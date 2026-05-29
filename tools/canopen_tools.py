#!/usr/bin/env python3
"""
canopen_tools.py — iSMART CANopen toolchain, single unified script.

XDD is the single source of truth for the device object dictionary.

Subcommands
-----------
  xdd2od        Generate CANopenNode V4 OD.c + OD.h from XDD
  xdd2eds       Generate EDS (CiA 306) from XDD
  eds2config    Generate firmware config-layer C artifacts from EDS
  validate      Validate an EDS or XDD file
  patch-desc    Insert ;Description= annotations into EDS

Build-system compatibility
--------------------------
When the first argument is a file path (*.eds or *.xdd) followed by -o <dir>,
the tool behaves as a drop-in for eds2c_wrapper.py — generates OD.c/OD.h from
the XDD that lives beside the given EDS file.

Auto-discovery
--------------
Without explicit --xdd / --eds flags the tool searches upward from its own
location for application/canopen/ismart-control-board.{xdd,eds}.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime
import re
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Repo-relative default paths (path-agnostic: works from any install location)
# ---------------------------------------------------------------------------
def _find_canopen_dir() -> Path:
    """Walk upward from this script to find the application/canopen/ directory.

    Works whether the script lives in application/scripts/ or in the
    CANopenNode module's tools/ directory.
    """
    for parent in Path(__file__).resolve().parents:
        candidate = parent / "application" / "canopen"
        if candidate.is_dir():
            return candidate
    # Fallback: assume co-located with an application/canopen/ sibling
    return Path(__file__).resolve().parent.parent / "application" / "canopen"


_CANOPEN_DIR  = _find_canopen_dir()
_DEFAULT_XDD  = _CANOPEN_DIR / "ismart-control-board.xdd"
_DEFAULT_EDS  = _CANOPEN_DIR / "ismart-control-board.eds"
_DEFAULT_REPO = _CANOPEN_DIR.parent.parent              # repo root


# ===========================================================================
# Data-type maps
# ===========================================================================

# XDD element tag -> (c_type, byte_size, is_float, is_string)
XDD_TYPE_MAP: Dict[str, Tuple[str, int, bool, bool]] = {
    "BOOL":           ("bool_t",    1, False, False),
    "SINT":           ("int8_t",    1, False, False),
    "USINT":          ("uint8_t",   1, False, False),
    "INT":            ("int16_t",   2, False, False),
    "UINT":           ("uint16_t",  2, False, False),
    "DINT":           ("int32_t",   4, False, False),
    "UDINT":          ("uint32_t",  4, False, False),
    "LINT":           ("int64_t",   8, False, False),
    "ULINT":          ("uint64_t",  8, False, False),
    "REAL":           ("float32_t", 4, True,  False),
    "LREAL":          ("double",    8, True,  False),
    "STRING":         ("char",      1, False, True),
    "OCTET_STRING":   ("uint8_t",   1, False, False),
    "UNICODE_STRING": ("uint16_t",  2, False, False),
}

# XDD access string -> EDS AccessType
ACCESS_TO_EDS: Dict[str, str] = {
    "read": "ro", "readonly": "ro", "ro": "ro", "const": "const",
    "write": "wo", "writeonly": "wo", "wo": "wo",
    "readwrite": "rw", "readWrite": "rw", "rw": "rw",
    "noAccess": "no", "no": "no",
}

# XDD data-type tag -> EDS DataType hex code
XDD_TO_EDS_DT: Dict[str, int] = {
    "BOOL": 0x0001, "SINT": 0x0002, "INT": 0x0003, "DINT": 0x0004,
    "USINT": 0x0005, "UINT": 0x0006, "UDINT": 0x0007, "REAL": 0x0008,
    "STRING": 0x0009, "OCTET_STRING": 0x000A, "UNICODE_STRING": 0x000B,
    "LINT": 0x0010, "LREAL": 0x0011, "ULINT": 0x001B,
}

# EDS DataType hex -> XDD-style info
EDS_DT_MAP: Dict[int, Tuple[str, int, bool, bool]] = {
    0x0001: ("bool_t",   1, False, False),
    0x0002: ("int8_t",   1, False, False),
    0x0003: ("int16_t",  2, False, False),
    0x0004: ("int32_t",  4, False, False),
    0x0005: ("uint8_t",  1, False, False),
    0x0006: ("uint16_t", 2, False, False),
    0x0007: ("uint32_t", 4, False, False),
    0x0008: ("float32_t",4, True,  False),
    0x0009: ("char",     1, False, True),
    0x000A: ("uint8_t",  1, False, False),
    0x000B: ("uint16_t", 2, False, False),
}

STORAGE_ORDER = ["PERSIST_COMM", "RAM", "PERSIST_APP", "ROM"]

STORAGE_OVERRIDE: Dict[int, str] = {
    0x1000: "PERSIST_COMM", 0x1001: "RAM", 0x1003: "RAM",
    0x1005: "PERSIST_COMM", 0x1006: "PERSIST_COMM", 0x1010: "RAM",
    0x1011: "RAM", 0x1012: "PERSIST_COMM", 0x1014: "PERSIST_COMM",
    0x1016: "PERSIST_COMM", 0x1017: "PERSIST_COMM", 0x1018: "RAM",
    0x1200: "RAM",
    0x1400: "PERSIST_COMM", 0x1401: "PERSIST_COMM", 0x1402: "PERSIST_COMM",
    0x1600: "PERSIST_COMM", 0x1601: "PERSIST_COMM", 0x1602: "PERSIST_COMM",
    0x1800: "PERSIST_COMM", 0x1801: "PERSIST_COMM",
    0x1802: "PERSIST_COMM", 0x1803: "PERSIST_COMM",
    0x1A00: "PERSIST_COMM", 0x1A01: "PERSIST_COMM",
    0x1A02: "PERSIST_COMM", 0x1A03: "PERSIST_COMM",
    0x2000: "RAM", 0x2001: "RAM", 0x2005: "RAM", 0x2010: "RAM",
    0x2011: "RAM", 0x2012: "RAM", 0x2013: "RAM", 0x2100: "RAM",
}


def _default_storage(index: int) -> str:
    if 0x1000 <= index <= 0x1FFF:
        return "PERSIST_COMM"
    if 0x2000 <= index <= 0x27FF:
        return "PERSIST_APP"
    return "RAM"


# ===========================================================================
# Shared data-classes
# ===========================================================================

@dataclasses.dataclass
class XddParam:
    uid: str
    name: str
    access: str          # XDD access string
    default_value: str
    data_type_tag: str   # e.g. "UDINT", "REAL", "STRING"
    storage_group: str
    pdo_mapping: str = "no"
    low_limit: str = ""
    high_limit: str = ""


@dataclasses.dataclass
class XddObject:
    index: int
    name: str
    object_type: int     # 7=VAR, 8=ARRAY, 9=RECORD
    pdo_mapping: str
    uid_ref: str
    sub_number: int
    param: Optional[XddParam] = None    # for VAR
    subs: List[Optional[XddParam]] = dataclasses.field(default_factory=list)  # ARRAY/RECORD


@dataclasses.dataclass
class EdsSub:
    index: int
    sub: int
    name: str
    parameter_name: str
    object_type: int
    storage: Optional[str]
    data_type: Optional[int]
    access_type: Optional[str]
    default: Optional[str]
    low_limit: Optional[str]
    high_limit: Optional[str]
    unit: Optional[str]
    gated: Optional[bool]
    enum_first: bool


@dataclasses.dataclass
class EdsObject:
    index: int
    name: str
    parameter_name: str
    object_type: int
    storage: Optional[str]
    sub_number: int
    subs: "OrderedDict[int, EdsSub]" = dataclasses.field(
        default_factory=OrderedDict)


# ===========================================================================
# XddParser
# ===========================================================================

class XddParser:
    """Parse a CANopen XDD file into XddObject / XddParam instances."""

    @staticmethod
    def _strip_ns(tag: str) -> str:
        return tag.split("}")[-1] if "}" in tag else tag

    @staticmethod
    def _strip_all_ns(elem: ET.Element) -> None:
        elem.tag = XddParser._strip_ns(elem.tag)
        for child in elem:
            XddParser._strip_all_ns(child)

    @staticmethod
    def _find_data_type_tag(element: ET.Element) -> str:
        for child in element:
            tag = XddParser._strip_ns(child.tag)
            if tag in XDD_TYPE_MAP:
                return tag
        return "UDINT"

    def parse(self, xdd_path: Path) -> List[XddObject]:
        """Return a list of XddObject in object-dictionary order."""
        tree = ET.parse(xdd_path)
        root = tree.getroot()
        self._strip_all_ns(root)

        # --- parameterList ---
        params: Dict[str, XddParam] = {}
        for pel in root.iter("parameter"):
            uid = pel.get("uniqueID", "")
            if not uid:
                continue
            access = pel.get("access", "readWrite")
            label_el = pel.find("label")
            name = label_el.text.strip() if label_el is not None and label_el.text else uid
            dt_tag = self._find_data_type_tag(pel)
            dv_el = pel.find("defaultValue")
            default = dv_el.get("value", "0") if dv_el is not None else "0"
            lo = hi = ""
            range_el = pel.find(".//range")
            if range_el is not None:
                min_e = range_el.find("minValue")
                max_e = range_el.find("maxValue")
                if min_e is not None:
                    lo = min_e.get("value", "")
                if max_e is not None:
                    hi = max_e.get("value", "")
            sg = ""
            for prop in pel.findall("property"):
                if prop.get("name") == "CO_storageGroup":
                    sg = prop.get("value", "")
            params[uid] = XddParam(uid=uid, name=name, access=access,
                                   default_value=default, data_type_tag=dt_tag,
                                   storage_group=sg, low_limit=lo, high_limit=hi)

        # --- CANopenObjectList ---
        objects: List[XddObject] = []
        col = root.find(".//CANopenObjectList")
        if col is None:
            raise RuntimeError("No CANopenObjectList found in XDD")

        for obj_el in col.findall("CANopenObject"):
            idx_str = obj_el.get("index", "0000")
            index = int(idx_str, 16)
            name = obj_el.get("name", f"obj_{idx_str}")
            obj_type = int(obj_el.get("objectType", "7"))
            pdo_map = obj_el.get("PDOmapping", "no")
            uid_ref = obj_el.get("uniqueIDRef", "")
            sub_num = int(obj_el.get("subNumber", "0"))

            entry = XddObject(index=index, name=name, object_type=obj_type,
                              pdo_mapping=pdo_map, uid_ref=uid_ref,
                              sub_number=sub_num)

            if obj_type == 7:
                p = params.get(uid_ref)
                if p:
                    p.pdo_mapping = pdo_map
                    if not p.storage_group:
                        p.storage_group = STORAGE_OVERRIDE.get(index, _default_storage(index))
                entry.param = p
            else:
                for sub_el in obj_el.findall("CANopenSubObject"):
                    sub_uid = sub_el.get("uniqueIDRef", "")
                    sub_pdo = sub_el.get("PDOmapping", "no")
                    p = params.get(sub_uid)
                    if p:
                        p.pdo_mapping = sub_pdo
                        if not p.storage_group:
                            p.storage_group = STORAGE_OVERRIDE.get(index, _default_storage(index))
                    entry.subs.append(p)

            objects.append(entry)

        return objects

    def parse_device_info(self, xdd_path: Path) -> dict:
        """Return a dict of device identity / file-info fields."""
        tree = ET.parse(xdd_path)
        root = tree.getroot()
        self._strip_all_ns(root)

        info: dict = {
            "file_name": xdd_path.stem + ".xdd",
            "file_version": "1",
            "vendor_name": "Unknown",
            "vendor_id": "0x00000000",
            "product_name": "Unknown Device",
            "product_id": "0x00000000",
            "description": "",
            "created_by": "",
            "creation_date": "",
            "creation_time": "",
            "modified_by": "",
            "modification_date": "",
            "modification_time": "",
            "nr_of_rxpdo": 0,
            "nr_of_txpdo": 0,
            "granularity": 8,
            "baud_rates": {"500": 1},
        }

        identity = root.find(".//DeviceIdentity")
        if identity is not None:
            for child in identity:
                tag = self._strip_ns(child.tag)
                text = (child.text or "").strip()
                if tag == "vendorName" and text:
                    info["vendor_name"] = text
                elif tag == "vendorID" and text:
                    info["vendor_id"] = text
                elif tag == "productName" and text:
                    info["product_name"] = text
                elif tag == "productID" and text:
                    info["product_id"] = text
                elif tag == "productText":
                    desc_el = child.find("description")
                    if desc_el is not None and desc_el.text:
                        info["description"] = desc_el.text.strip()

        for pb in root.findall(".//ProfileBody"):
            fn = pb.get("fileName", "")
            if fn:
                info["file_name"] = fn
            fv = pb.get("fileVersion", "")
            if fv:
                info["file_version"] = fv
            for k, attr in [("created_by", "fileCreator"),
                             ("modified_by", "fileModifiedBy"),
                             ("creation_date", "fileCreationDate"),
                             ("creation_time", "fileCreationTime"),
                             ("modification_date", "fileModificationDate"),
                             ("modification_time", "fileModificationTime")]:
                val = pb.get(attr, "")
                if val:
                    info[k] = val

        gf = root.find(".//CANopenGeneralFeatures")
        if gf is not None:
            info["nr_of_rxpdo"] = int(gf.get("nrOfRxPDO", "0"))
            info["nr_of_txpdo"] = int(gf.get("nrOfTxPDO", "0"))
            info["granularity"] = int(gf.get("granularity", "8"))

        phys = root.find(".//PhysicalLayer/baudRate")
        if phys is not None:
            brs: Dict[str, int] = {}
            for br_el in phys.findall("supportedBaudRate"):
                m = re.match(r"(\d+)", br_el.get("value", ""))
                if m:
                    brs[m.group(1)] = 1
            if brs:
                info["baud_rates"] = brs

        return info


# ===========================================================================
# EdsParser  (our own, no eds_utils — handles '=' inside comments/values)
# ===========================================================================

class EdsParser:
    """Parse an EDS/DCF file into EdsObject instances.

    Correctly handles values that contain '=' (uses split('=', 1))
    and skips non-key=value comment lines.
    """

    CONFIG_LO = 0x2200
    CONFIG_HI = 0x22FF

    @staticmethod
    def _parse_sections(text: str) -> "OrderedDict[str, List[str]]":
        sections: "OrderedDict[str, List[str]]" = OrderedDict()
        current: Optional[str] = None
        body: List[str] = []
        for line in text.splitlines():
            m = re.match(r"^\[([^\]]+)\]\s*$", line)
            if m:
                if current is not None:
                    sections[current] = body
                current, body = m.group(1), []
            elif current is not None:
                body.append(line)
        if current is not None:
            sections[current] = body
        return sections

    @staticmethod
    def _parse_kv(lines: List[str]) -> Dict[str, str]:
        out: Dict[str, str] = {}
        for line in lines:
            s = line.strip()
            if not s:
                continue
            if s.startswith(";"):
                payload = s[1:]
                if "=" in payload:
                    k, v = payload.split("=", 1)
                    out[f";{k.strip()}"] = v.strip()
                continue
            if "=" in s:
                k, v = s.split("=", 1)
                out[k.strip()] = v.strip()
        return out

    @staticmethod
    def _boolish(s: str) -> bool:
        return s.strip().lower() in ("1", "true", "yes", "y")

    def parse(self, eds_path: Path,
              index_range: Optional[Tuple[int, int]] = None
              ) -> "OrderedDict[int, EdsObject]":
        """Parse EDS and return objects within optional (lo, hi) index range."""
        sections = self._parse_sections(eds_path.read_text(encoding="utf-8",
                                                            errors="replace"))
        objects: "OrderedDict[int, EdsObject]" = OrderedDict()

        for sec_name, lines in sections.items():
            if not re.fullmatch(r"[0-9A-Fa-f]{4}", sec_name):
                continue
            idx = int(sec_name, 16)
            if index_range and not (index_range[0] <= idx <= index_range[1]):
                continue
            kv = self._parse_kv(lines)
            if "ObjectType" not in kv:
                continue
            obj_type = int(kv["ObjectType"], 0)
            obj = EdsObject(
                index=idx,
                name=sec_name,
                parameter_name=kv.get("ParameterName", sec_name),
                object_type=obj_type,
                storage=kv.get(";StorageLocation"),
                sub_number=int(kv.get("SubNumber", "0"), 0),
            )
            objects[idx] = obj

            if obj_type == 0x7:  # VAR — synthesise sub-0
                name = kv.get("ParameterName", sec_name).split(" - ", 1)[0].strip()
                objects[idx].subs[0] = EdsSub(
                    index=idx, sub=0, name=name,
                    parameter_name=kv.get("ParameterName", sec_name),
                    object_type=0x7,
                    storage=kv.get(";StorageLocation"),
                    data_type=int(kv["DataType"], 0) if "DataType" in kv else None,
                    access_type=kv.get("AccessType"),
                    default=kv.get("DefaultValue"),
                    low_limit=kv.get("LowLimit"),
                    high_limit=kv.get("HighLimit"),
                    unit=kv.get(";Unit"),
                    gated=self._boolish(kv[";Gated"]) if ";Gated" in kv else None,
                    enum_first=self._boolish(kv.get(";EnumFirst", "false")),
                )

        # Second pass — sub-sections
        for sec_name, lines in sections.items():
            m = re.fullmatch(r"([0-9A-Fa-f]{4})sub(\d+)", sec_name)
            if not m:
                continue
            idx = int(m.group(1), 16)
            sub = int(m.group(2), 10)
            if idx not in objects:
                continue
            kv = self._parse_kv(lines)
            pname = kv.get("ParameterName", sec_name)
            name = pname.split(" - ", 1)[0].strip()
            if objects[idx].object_type == 0x8 and sub != 0:
                name = re.sub(r"_\d+$", "", name)
            objects[idx].subs[sub] = EdsSub(
                index=idx, sub=sub, name=name, parameter_name=pname,
                object_type=int(kv.get("ObjectType", "7"), 0),
                storage=kv.get(";StorageLocation"),
                data_type=int(kv["DataType"], 0) if "DataType" in kv else None,
                access_type=kv.get("AccessType"),
                default=kv.get("DefaultValue"),
                low_limit=kv.get("LowLimit"),
                high_limit=kv.get("HighLimit"),
                unit=kv.get(";Unit"),
                gated=self._boolish(kv[";Gated"]) if ";Gated" in kv else None,
                enum_first=self._boolish(kv.get(";EnumFirst", "false")),
            )

        return objects

    def validate(self, file_path: Path) -> List[str]:
        """Return list of error strings; empty means valid."""
        errors: List[str] = []
        text = file_path.read_text(encoding="utf-8", errors="replace")
        sections = self._parse_sections(text)
        for sec_name, lines in sections.items():
            if not re.fullmatch(r"\[?[0-9A-Fa-f]{4}(sub\d+)?\]?", sec_name):
                continue
            for line in lines:
                s = line.strip()
                if not s or s.startswith(";"):
                    continue
                if "=" not in s:
                    errors.append(f"[{sec_name}] missing '=': {s!r}")
        return errors


# ===========================================================================
# OdGenerator  — XDD -> OD.c + OD.h
# ===========================================================================

class OdGenerator:
    """Generate CANopenNode V4 OD.c and OD.h from parsed XDD objects."""

    STRUCT_MAP = {
        "PERSIST_COMM": "OD_PERSIST_COMM_t",
        "RAM":          "OD_RAM_t",
        "PERSIST_APP":  "OD_PERSIST_APP_t",
        "ROM":          "OD_ROM_t",
    }
    ATTR_MAP = {
        "PERSIST_COMM": "OD_ATTR_PERSIST_COMM",
        "RAM":          "OD_ATTR_RAM",
        "PERSIST_APP":  "OD_ATTR_PERSIST_APP",
        "ROM":          "OD_ATTR_ROM",
    }

    # ---- helpers ------------------------------------------------------------

    @staticmethod
    def _c_ident(name: str) -> str:
        return re.sub(r"[^0-9a-zA-Z_]", "_", name)

    @classmethod
    def _var_name(cls, index: int, name: str) -> str:
        return f"x{index:04X}_{cls._c_ident(name)}"

    @classmethod
    def _obj_name(cls, index: int, name: str) -> str:
        return f"o_{index:04X}_{cls._c_ident(name)}"

    @staticmethod
    def _field_name(p: Optional[XddParam]) -> str:
        return OdGenerator._c_ident(p.name) if p else "reserved"

    @staticmethod
    def _access_to_attr(access: str, pdo: str, dt_tag: str) -> str:
        _, size, _, is_str = XDD_TYPE_MAP.get(dt_tag, ("uint32_t", 4, False, False))
        a = (access or "readWrite").lower()
        if a in ("read", "const", "readonly", "ro"):
            parts = ["ODA_SDO_R"]
        elif a in ("write", "writeonly", "wo"):
            parts = ["ODA_SDO_W"]
        else:
            parts = ["ODA_SDO_RW"]
        if (pdo or "no").lower() in ("optional", "default"):
            parts.append("ODA_TRPDO")
        if size > 1 and not is_str:
            parts.append("ODA_MB")
        if is_str:
            parts.append("ODA_STR")
        return " | ".join(parts)

    @staticmethod
    def _resolve_default(default_val: str, dt_tag: str, index: int = 0) -> str:
        if not default_val:
            return "0"
        if "$NODEID" in default_val:
            m = re.search(r"\$NODEID\+?(0x[0-9a-fA-F]+|\d+)?", default_val)
            offset = int(m.group(1), 0) if (m and m.group(1)) else 0
            return f"0x{offset:08X}"
        _, size, is_float, is_str = XDD_TYPE_MAP.get(dt_tag, ("uint32_t", 4, False, False))
        if is_str:
            return None  # handled separately
        if is_float:
            try:
                v = float(default_val)
                return f"{v:.1f}" if v == int(v) else str(v)
            except ValueError:
                return "0.0"
        try:
            iv = int(default_val, 0) if default_val.startswith(("0x", "0X")) else int(default_val)
            if size == 1:
                return f"0x{iv & 0xFF:02X}"
            elif size == 2:
                return f"0x{iv & 0xFFFF:04X}"
            return f"0x{iv & 0xFFFFFFFF:08X}"
        except ValueError:
            return "0"

    @staticmethod
    def _group_by_storage(objects: List[XddObject]) -> Dict[str, List[XddObject]]:
        groups: Dict[str, List[XddObject]] = {sg: [] for sg in STORAGE_ORDER}
        for obj in objects:
            if obj.object_type == 7:
                sg = obj.param.storage_group if obj.param else "RAM"
            else:
                sg = "RAM"
                for s in obj.subs:
                    if s:
                        sg = s.storage_group
                        break
            groups.setdefault(sg, []).append(obj)
        return groups

    # ---- OD.h ---------------------------------------------------------------

    def gen_h(self, objects: List[XddObject], info: dict) -> str:
        # Sort by index so OD_ENTRY_Hxxxx shortcuts match the sorted ODList.
        objects = sorted(objects, key=lambda o: o.index)
        L: List[str] = []
        w = L.append

        w(f"/*{'*' * 78}")
        w(f"    CANopen Object Dictionary definition for CANopenNode V4")
        w(f"")
        w(f"    Generated by canopen_tools.py (iSMART firmware build tool)")
        w(f"")
        w(f"    File info:")
        w(f"        File Names:   OD.h; OD.c")
        w(f"        Project File: {info['file_name']}")
        w(f"        File Version: {info['file_version']}")
        for field, label in [("creation_date", "Created"),
                              ("created_by", "Created By"),
                              ("modification_date", "Modified"),
                              ("modified_by", "Modified By")]:
            if info.get(field):
                w(f"        {label}: {info[field]}")
        w(f"")
        w(f"    Device Info:")
        w(f"        Vendor Name:  {info['vendor_name']}")
        w(f"        Vendor ID:    {info['vendor_id']}")
        w(f"        Product Name: {info['product_name']}")
        w(f"        Product ID:   {info['product_id']}")
        if info.get("description"):
            w(f"        Description:  {info['description']}")
        w(f"{'*' * 79}/")
        w("")
        w("#ifndef OD_H")
        w("#define OD_H")
        w("")
        # Include the CANopenNode OD interface header so OD.h is self-contained
        # and can be included from any translation unit without needing the full
        # CANopen stack headers to be pre-included.  CO_ODinterface.h chains to
        # CO_driver.h which defines float32_t, bool_t, OD_t, OD_entry_t, etc.
        w('#include "301/CO_ODinterface.h"')
        w("")

        # Counters
        w("/*" + "*" * 78)
        w("    Counters of OD objects")
        w("*" * 79 + "/")
        w("#define OD_CNT_NMT 1")
        w("#define OD_CNT_EM 1")
        w("#define OD_CNT_SYNC 1")
        w("#define OD_CNT_SYNC_PROD 1")
        w("#define OD_CNT_STORAGE 1")
        w("#define OD_CNT_TIME 1")
        w("#define OD_CNT_EM_PROD 1")
        w("#define OD_CNT_HB_CONS 1")
        w("#define OD_CNT_HB_PROD 1")
        w("#define OD_CNT_SDO_SRV 1")
        rpdo = sum(1 for o in objects if 0x1400 <= o.index <= 0x14FF)
        tpdo = sum(1 for o in objects if 0x1800 <= o.index <= 0x18FF)
        w(f"#define OD_CNT_RPDO {rpdo}")
        w(f"#define OD_CNT_TPDO {tpdo}")
        w("")

        # Array sizes
        w("/*" + "*" * 78)
        w("    Sizes of OD arrays")
        w("*" * 79 + "/")
        for obj in objects:
            if obj.object_type == 8:
                w(f"#define OD_CNT_ARR_{obj.index:04X} {len(obj.subs) - 1}")
        w("")

        # Struct typedefs
        w("/*" + "*" * 78)
        w("    OD data declaration of all groups")
        w("*" * 79 + "/")
        groups = self._group_by_storage(objects)
        for sg in STORAGE_ORDER:
            grp = groups.get(sg, [])
            if not grp:
                continue
            w(f"typedef struct {{")
            for obj in grp:
                vname = self._var_name(obj.index, obj.name)
                if obj.object_type == 7:
                    p = obj.param
                    if p:
                        ctype, _, _, is_str = XDD_TYPE_MAP.get(p.data_type_tag, ("uint32_t", 4, False, False))
                        if is_str:
                            slen = len(p.default_value) + 1
                            w(f"    {ctype} {vname}[{slen}];")
                        else:
                            w(f"    {ctype} {vname};")
                elif obj.object_type == 8:
                    sub1 = obj.subs[1] if len(obj.subs) > 1 else None
                    ctype = XDD_TYPE_MAP.get(sub1.data_type_tag if sub1 else "UDINT", ("uint32_t",))[0]
                    w(f"    uint8_t {vname}_sub0;")
                    w(f"    {ctype} {vname}[OD_CNT_ARR_{obj.index:04X}];")
                else:
                    w(f"    struct {{")
                    for sub in obj.subs:
                        if sub:
                            ctype = XDD_TYPE_MAP.get(sub.data_type_tag, ("uint32_t",))[0]
                            w(f"        {ctype} {self._field_name(sub)};")
                    w(f"    }} {vname};")
            w(f"}} {self.STRUCT_MAP[sg]};")
            w("")

        # Extern declarations
        for sg in STORAGE_ORDER:
            if not groups.get(sg):
                continue
            stype = self.STRUCT_MAP[sg]
            attr = self.ATTR_MAP[sg]
            w(f"#ifndef {attr}")
            w(f"#define {attr}")
            w(f"#endif")
            w(f"extern {attr} {stype} {stype[:-2]};")
            w("")

        w("#ifndef OD_ATTR_OD")
        w("#define OD_ATTR_OD")
        w("#endif")
        w("extern OD_ATTR_OD OD_t *OD;")
        w("")

        # Entry shortcuts
        w("/*" + "*" * 78)
        w("    Object dictionary entries - shortcuts")
        w("*" * 79 + "/")
        for i, obj in enumerate(objects):
            w(f"#define OD_ENTRY_H{obj.index:04X} &OD->list[{i}]")
        w("")

        w("/*" + "*" * 78)
        w("    Object dictionary entries - shortcuts with names")
        w("*" * 79 + "/")
        for i, obj in enumerate(objects):
            w(f"#define OD_ENTRY_H{obj.index:04X}_{self._c_ident(obj.name)} &OD->list[{i}]")
        w("")

        # OD_INIT_CONFIG
        w("/*" + "*" * 78)
        w("    OD config structure")
        w("*" * 79 + "/")
        w("#ifdef CO_MULTIPLE_OD")
        w("#define OD_INIT_CONFIG(config) {\\")
        for line in [
            "    (config).CNT_NMT = OD_CNT_NMT;",
            "    (config).ENTRY_H1017 = OD_ENTRY_H1017;",
            "    (config).CNT_HB_CONS = OD_CNT_HB_CONS;",
            "    (config).CNT_ARR_1016 = OD_CNT_ARR_1016;",
            "    (config).ENTRY_H1016 = OD_ENTRY_H1016;",
            "    (config).CNT_EM = OD_CNT_EM;",
            "    (config).ENTRY_H1001 = OD_ENTRY_H1001;",
            "    (config).ENTRY_H1014 = OD_ENTRY_H1014;",
            "    (config).ENTRY_H1015 = NULL;",
            "    (config).CNT_ARR_1003 = OD_CNT_ARR_1003;",
            "    (config).ENTRY_H1003 = OD_ENTRY_H1003;",
            "    (config).CNT_SDO_SRV = OD_CNT_SDO_SRV;",
            "    (config).ENTRY_H1200 = OD_ENTRY_H1200;",
            "    (config).CNT_SDO_CLI = 0;",
            "    (config).ENTRY_H1280 = NULL;",
            "    (config).CNT_TIME = OD_CNT_TIME;",
            "    (config).ENTRY_H1012 = OD_ENTRY_H1012;",
            "    (config).CNT_SYNC = OD_CNT_SYNC;",
            "    (config).ENTRY_H1005 = OD_ENTRY_H1005;",
            "    (config).ENTRY_H1006 = OD_ENTRY_H1006;",
            "    (config).ENTRY_H1007 = NULL;",
            "    (config).ENTRY_H1019 = NULL;",
            f"    (config).CNT_RPDO = OD_CNT_RPDO;",
            "    (config).ENTRY_H1400 = OD_ENTRY_H1400;",
            "    (config).ENTRY_H1600 = OD_ENTRY_H1600;",
            f"    (config).CNT_TPDO = OD_CNT_TPDO;",
            "    (config).ENTRY_H1800 = OD_ENTRY_H1800;",
            "    (config).ENTRY_H1A00 = OD_ENTRY_H1A00;",
            "    (config).CNT_LEDS = 0;",
            "    (config).CNT_GFC = 0;",
            "    (config).ENTRY_H1300 = NULL;",
            "    (config).CNT_SRDO = 0;",
            "    (config).ENTRY_H1301 = NULL;",
            "    (config).ENTRY_H1381 = NULL;",
            "    (config).ENTRY_H13FE = NULL;",
            "    (config).ENTRY_H13FF = NULL;",
            "    (config).CNT_LSS_SLV = 0;",
            "    (config).CNT_LSS_MST = 0;",
            "    (config).CNT_GTWA = 0;",
            "    (config).CNT_TRACE = 0;",
        ]:
            w(line + "\\")
        w("}")
        w("#endif")
        w("")
        w("#endif /* OD_H */")

        return "\n".join(L) + "\n"

    # ---- OD.c ---------------------------------------------------------------

    def gen_c(self, objects: List[XddObject], info: dict) -> str:
        # Sort by index so ODObjs_t field order and ODList are consistent.
        objects = sorted(objects, key=lambda o: o.index)
        L: List[str] = []
        w = L.append

        w(f"/*{'*' * 78}")
        w(f"    CANopen Object Dictionary definition for CANopenNode V4")
        w(f"")
        w(f"    Generated by canopen_tools.py (iSMART firmware build tool)")
        w(f"")
        w(f"    DON'T EDIT THIS FILE MANUALLY, UNLESS YOU KNOW WHAT YOU ARE DOING !!!!")
        w(f"{'*' * 79}/")
        w("")
        w("#define OD_DEFINITION")
        w('#include "301/CO_ODinterface.h"')
        w('#include "OD.h"')
        w("")
        w("#if CO_VERSION_MAJOR < 4")
        w("#error This Object dictionary is compatible with CANopenNode V4.0 and above!")
        w("#endif")

        groups = self._group_by_storage(objects)

        # Data initializations
        w("")
        w("/*" + "*" * 78)
        w("    OD data initialization of all groups")
        w("*" * 79 + "/")

        for sg in STORAGE_ORDER:
            grp = groups.get(sg, [])
            if not grp:
                continue
            stype = self.STRUCT_MAP[sg]
            attr = self.ATTR_MAP[sg]
            varname = stype[:-2]
            w(f"{attr} {stype} {varname} = {{")
            for obj in grp:
                vname = self._var_name(obj.index, obj.name)
                if obj.object_type == 7:
                    p = obj.param
                    if not p:
                        continue
                    _, _, _, is_str = XDD_TYPE_MAP.get(p.data_type_tag, ("uint32_t", 4, False, False))
                    if is_str:
                        chars = ", ".join(f"'{c}'" for c in p.default_value) + ", 0"
                        w(f"    .{vname} = {{{chars}}},")
                    else:
                        dv = self._resolve_default(p.default_value, p.data_type_tag, obj.index)
                        w(f"    .{vname} = {dv},")
                elif obj.object_type == 8:
                    sub0 = obj.subs[0]
                    subs_rest = obj.subs[1:]
                    if sub0:
                        dv0 = self._resolve_default(sub0.default_value, sub0.data_type_tag)
                        w(f"    .{vname}_sub0 = {dv0},")
                    if subs_rest:
                        p1 = subs_rest[0]
                        _, _, is_float, _ = XDD_TYPE_MAP.get(
                            p1.data_type_tag if p1 else "UDINT", ("uint32_t", 4, False, False))
                        dv_elem = "0.0" if is_float else "0x00000000"
                        vals = ", ".join(dv_elem for _ in subs_rest)
                        w(f"    .{vname} = {{{vals}}},")
                else:
                    w(f"    .{vname} = {{")
                    for sub in obj.subs:
                        if not sub:
                            continue
                        fname = self._field_name(sub)
                        _, _, is_float, is_str = XDD_TYPE_MAP.get(
                            sub.data_type_tag, ("uint32_t", 4, False, False))
                        if is_str:
                            chars = ", ".join(f"'{c}'" for c in sub.default_value) + ", 0"
                            w(f"        .{fname} = {{{chars}}},")
                        else:
                            dv = self._resolve_default(sub.default_value, sub.data_type_tag, obj.index)
                            w(f"        .{fname} = {dv},")
                    w(f"    }},")
            w(f"}};")
            w("")

        # ODObjs_t typedef
        w("")
        w("/*" + "*" * 78)
        w("    All OD objects (constant definitions)")
        w("*" * 79 + "/")
        w("typedef struct {")
        for obj in objects:
            oname = self._obj_name(obj.index, obj.name)
            if obj.object_type == 7:
                w(f"    OD_obj_var_t {oname};")
            elif obj.object_type == 8:
                w(f"    OD_obj_array_t {oname};")
            else:
                w(f"    OD_obj_record_t {oname}[{len(obj.subs)}];")
        w("} ODObjs_t;")
        w("")
        w("static CO_PROGMEM ODObjs_t ODObjs = {")

        for obj in objects:
            oname = self._obj_name(obj.index, obj.name)
            if obj.object_type == 7:
                sg = obj.param.storage_group if obj.param else "RAM"
            else:
                sg = "RAM"
                for s in obj.subs:
                    if s:
                        sg = s.storage_group
                        break
            sg_var = self.STRUCT_MAP.get(sg, "OD_RAM_t")[:-2]
            vname = self._var_name(obj.index, obj.name)

            if obj.object_type == 7:
                p = obj.param
                if not p:
                    continue
                attr = self._access_to_attr(p.access, p.pdo_mapping, p.data_type_tag)
                _, size, _, is_str = XDD_TYPE_MAP.get(p.data_type_tag, ("uint32_t", 4, False, False))
                w(f"    .{oname} = {{")
                if is_str:
                    slen = len(p.default_value)
                    w(f"        .dataOrig = &{sg_var}.{vname}[0],")
                    w(f"        .attribute = {attr},")
                    w(f"        .dataLength = {slen}")
                else:
                    w(f"        .dataOrig = &{sg_var}.{vname},")
                    w(f"        .attribute = {attr},")
                    w(f"        .dataLength = {size}")
                w(f"    }},")
            elif obj.object_type == 8:
                sub0 = obj.subs[0] if obj.subs else None
                sub1 = obj.subs[1] if len(obj.subs) > 1 else None
                attr0 = self._access_to_attr(
                    sub0.access if sub0 else "readOnly", "no",
                    sub0.data_type_tag if sub0 else "USINT")
                p1dt = sub1.data_type_tag if sub1 else "UDINT"
                attr1 = self._access_to_attr(
                    sub1.access if sub1 else "readWrite",
                    sub1.pdo_mapping if sub1 else "no", p1dt)
                _, elem_size, _, _ = XDD_TYPE_MAP.get(p1dt, ("uint32_t", 4, False, False))
                ctype1 = XDD_TYPE_MAP.get(p1dt, ("uint32_t",))[0]
                w(f"    .{oname} = {{")
                w(f"        .dataOrig0 = &{sg_var}.{vname}_sub0,")
                w(f"        .dataOrig = &{sg_var}.{vname}[0],")
                w(f"        .attribute0 = {attr0},")
                w(f"        .attribute = {attr1},")
                w(f"        .dataElementLength = {elem_size},")
                w(f"        .dataElementSizeof = sizeof({ctype1})")
                w(f"    }},")
            else:
                w(f"    .{oname} = {{")
                for i, sub in enumerate(obj.subs):
                    if not sub:
                        w(f"        {{")
                        w(f"            .dataOrig = NULL,")
                        w(f"            .subIndex = {i},")
                        w(f"            .attribute = ODA_SDO_RW,")
                        w(f"            .dataLength = 1")
                        w(f"        }},")
                        continue
                    fname = self._field_name(sub)
                    attr = self._access_to_attr(sub.access, sub.pdo_mapping, sub.data_type_tag)
                    _, size, _, _ = XDD_TYPE_MAP.get(sub.data_type_tag, ("uint32_t", 4, False, False))
                    w(f"        {{")
                    w(f"            .dataOrig = &{sg_var}.{vname}.{fname},")
                    w(f"            .subIndex = {i},")
                    w(f"            .attribute = {attr},")
                    w(f"            .dataLength = {size}")
                    w(f"        }},")
                w(f"    }},")

        w("};")

        # ODList - entries MUST be sorted by index ascending.
        # OD_find() in CANopenNode V4 uses binary search; an out-of-order
        # entry causes CO_ERROR_OD_PARAMETERS during CO_CANopenInitPDO.
        w("")
        w("")
        w("/*" + "*" * 78)
        w("    Object dictionary")
        w("*" * 79 + "/")
        w("static OD_ATTR_OD OD_entry_t ODList[] = {")
        for obj in sorted(objects, key=lambda o: o.index):
            oname = self._obj_name(obj.index, obj.name)
            if obj.object_type == 7:
                cnt, odt = 1, "ODT_VAR"
            elif obj.object_type == 8:
                cnt, odt = len(obj.subs), "ODT_ARR"
            else:
                cnt, odt = len(obj.subs), "ODT_REC"
            w(f"    {{0x{obj.index:04X}, 0x{cnt:02X}, {odt}, &ODObjs.{oname}, NULL}},")
        w("    {0x0000, 0x00, 0, NULL, NULL}")
        w("};")
        w("")
        w("static OD_t _OD = {")
        w("    (sizeof(ODList) / sizeof(ODList[0])) - 1,")
        w("    &ODList[0]")
        w("};")
        w("")
        w("OD_ATTR_OD OD_t *OD = &_OD;")
        w("")

        return "\n".join(L) + "\n"

    def generate(self, xdd_path: Path, out_dir: Path) -> Tuple[Path, Path]:
        """Parse XDD and write OD.c + OD.h into out_dir. Returns (od_c, od_h)."""
        out_dir.mkdir(parents=True, exist_ok=True)
        parser = XddParser()
        objects = parser.parse(xdd_path)
        info = parser.parse_device_info(xdd_path)
        print(f"  {len(objects)} objects parsed from {xdd_path.name}")
        out_h = out_dir / "OD.h"
        out_c = out_dir / "OD.c"
        out_h.write_text(self.gen_h(objects, info), encoding="utf-8")
        out_c.write_text(self.gen_c(objects, info), encoding="utf-8")
        return out_c, out_h


# ===========================================================================
# EdsGenerator  — XDD -> EDS
# ===========================================================================

class EdsGenerator:
    """Generate a CiA 306-compliant EDS from XDD."""

    @staticmethod
    def _eds_access(access: str) -> str:
        return ACCESS_TO_EDS.get(access.lower(), ACCESS_TO_EDS.get(access, "rw"))

    @staticmethod
    def _pdo_map_int(pdo: str) -> int:
        return 1 if pdo.lower() in ("optional", "default") else 0

    def _write_var(self, lines: List[str], index: int, name: str, pdo: str, p: Optional[XddParam]) -> None:
        w = lines.append
        dt_tag = p.data_type_tag if p else "UDINT"
        dt_code = XDD_TO_EDS_DT.get(dt_tag, 0x0007)
        access = self._eds_access(p.access if p else "rw")
        default = p.default_value if p else ""
        lo = p.low_limit if p else ""
        hi = p.high_limit if p else ""
        sg = p.storage_group if p else ""
        w(f"[{index:04X}]")
        w(f"ParameterName={name}")
        if sg:
            w(f";StorageLocation={sg}")
        w(f"ObjectType=0x7")
        w(f"DataType=0x{dt_code:04X}")
        w(f"AccessType={access}")
        if default:
            w(f"DefaultValue={default}")
        if hi:
            w(f"HighLimit={hi}")
        if lo:
            w(f"LowLimit={lo}")
        w(f"PDOMapping={self._pdo_map_int(pdo)}")
        w("")

    def _write_array_record(self, lines: List[str], obj: XddObject) -> None:
        w = lines.append
        ot = obj.object_type
        subs = obj.subs
        sg = ""
        if subs and subs[0]:
            sg = subs[0].storage_group
        w(f"[{obj.index:04X}]")
        w(f"ParameterName={obj.name}")
        if sg:
            w(f";StorageLocation={sg}")
        w(f"ObjectType=0x{ot:X}")
        w(f"SubNumber=0x{len(subs):02X}")
        w("")
        for i, sub in enumerate(subs):
            dt_tag = sub.data_type_tag if sub else "UDINT"
            dt_code = XDD_TO_EDS_DT.get(dt_tag, 0x0007)
            sub_name = sub.name if sub else f"sub{i:02X}"
            access = self._eds_access(sub.access if sub else "rw")
            default = sub.default_value if sub else ""
            lo = sub.low_limit if sub else ""
            hi = sub.high_limit if sub else ""
            sub_sg = sub.storage_group if sub else ""
            sub_pdo = sub.pdo_mapping if sub else "no"
            w(f"[{obj.index:04X}sub{i}]")
            w(f"ParameterName={sub_name}")
            w(f"ObjectType=0x7")
            if sub_sg:
                w(f";StorageLocation={sub_sg}")
            w(f"DataType=0x{dt_code:04X}")
            w(f"AccessType={access}")
            if default:
                w(f"DefaultValue={default}")
            if hi:
                w(f"HighLimit={hi}")
            if lo:
                w(f"LowLimit={lo}")
            w(f"PDOMapping={self._pdo_map_int(sub_pdo)}")
            w("")

    def generate(self, xdd_path: Path, out_path: Path) -> None:
        """Generate EDS from XDD, write to out_path."""
        parser = XddParser()
        objects = parser.parse(xdd_path)
        info = parser.parse_device_info(xdd_path)
        now = datetime.datetime.now()
        date_str = now.strftime("%m-%d-%Y")
        time_str = now.strftime("%I:%M%p")

        L: List[str] = []
        w = L.append

        w("[FileInfo]")
        w(f"FileName={out_path.name}")
        w("FileVersion=1")
        w("FileRevision=1")
        w("LastEDS=")
        w("EDSVersion=4.0")
        w(f"Description={info.get('description', '')}")
        w(f"CreationTime={info.get('creation_time', time_str) or time_str}")
        w(f"CreationDate={info.get('creation_date', date_str) or date_str}")
        w(f"CreatedBy={info.get('created_by', 'canopen_tools.py') or 'canopen_tools.py'}")
        w(f"ModificationTime={time_str}")
        w(f"ModificationDate={date_str}")
        w(f"ModifiedBy=canopen_tools.py")
        w("")

        w("[DeviceInfo]")
        w(f"VendorName={info['vendor_name']}")
        w(f"VendorNumber={info['vendor_id']}")
        w(f"ProductName={info['product_name']}")
        w(f"ProductNumber={info['product_id']}")
        w("RevisionNumber=0")
        br_map = info.get("baud_rates", {"500": 1})
        for br in ["10", "20", "50", "125", "250", "500", "800", "1000"]:
            w(f"BaudRate_{br}={br_map.get(br, 0)}")
        w("SimpleBootUpMaster=0")
        w("SimpleBootUpSlave=1")
        w(f"Granularity={info.get('granularity', 8)}")
        w("DynamicChannelsSupported=0")
        w("CompactPDO=0")
        w("GroupMessaging=0")
        w(f"NrOfRXPDO={info.get('nr_of_rxpdo', 0)}")
        w(f"NrOfTXPDO={info.get('nr_of_txpdo', 0)}")
        w("LSS_Supported=0")
        w("NG_Slave=0")
        w("")

        w("[DummyUsage]")
        for i in range(1, 8):
            w(f"Dummy{i:04d}=0")
        w("")

        w("[Comments]")
        w("Lines=0")
        w("")

        mandatory, optional_, manufacturer = [], [], []
        for obj in objects:
            if obj.index in (0x1000, 0x1001, 0x1018):
                mandatory.append(obj)
            elif 0x1002 <= obj.index <= 0x1FFF:
                optional_.append(obj)
            else:
                manufacturer.append(obj)

        def write_obj_list(section: str, objs: List[XddObject]) -> None:
            w(f"[{section}]")
            w(f"SupportedObjects={len(objs)}")
            for i, o in enumerate(objs, 1):
                w(f"{i}=0x{o.index:04X}")
            w("")

        write_obj_list("MandatoryObjects", mandatory)
        for obj in mandatory:
            if obj.object_type == 7:
                self._write_var(L, obj.index, obj.name, obj.pdo_mapping, obj.param)
            else:
                self._write_array_record(L, obj)

        write_obj_list("OptionalObjects", optional_)
        for obj in optional_:
            if obj.object_type == 7:
                self._write_var(L, obj.index, obj.name, obj.pdo_mapping, obj.param)
            else:
                self._write_array_record(L, obj)

        write_obj_list("ManufacturerObjects", manufacturer)
        for obj in manufacturer:
            if obj.object_type == 7:
                self._write_var(L, obj.index, obj.name, obj.pdo_mapping, obj.param)
            else:
                self._write_array_record(L, obj)

        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n".join(L) + "\n", encoding="utf-8")
        print(f"  Wrote {out_path} ({len(objects)} objects)")


# ===========================================================================
# ConfigGenerator  — EDS -> config C artifacts
# ===========================================================================

class ConfigGenerator:
    """Generate firmware config-layer C artifacts from EDS manufacturer objects."""

    DT_BOOLEAN       = 0x0001
    DT_INTEGER32     = 0x0004
    DT_UNSIGNED8     = 0x0005
    DT_UNSIGNED32    = 0x0007
    DT_REAL32        = 0x0008
    DT_VISIBLE_STRING = 0x0009

    @staticmethod
    def _strip_float_suffix(s: str) -> str:
        return re.sub(r"(?<=\d)[fF]\b", "", s)

    @classmethod
    def _scalar_cfg_type(cls, dt: int) -> str:
        if dt in (cls.DT_UNSIGNED32, cls.DT_INTEGER32):
            return "eInteger"
        if dt == cls.DT_REAL32:
            return "eFloat"
        if dt == cls.DT_VISIBLE_STRING:
            return "eString"
        raise ValueError(f"Unsupported scalar EDS DataType 0x{dt:04X}")

    @classmethod
    def _array_cfg_type(cls, dt: int) -> str:
        if dt in (cls.DT_UNSIGNED32, cls.DT_INTEGER32):
            return "eIntArray"
        if dt == cls.DT_REAL32:
            return "eFloatArray"
        raise ValueError(f"Unsupported array element EDS DataType 0x{dt:04X}")

    @classmethod
    def _c_literal(cls, value: str, dt: int) -> str:
        v = value.strip()
        if dt == cls.DT_VISIBLE_STRING:
            return f'"{v}"'
        if dt == cls.DT_REAL32:
            v = cls._strip_float_suffix(v)
            if "." not in v and "e" not in v.lower():
                v = f"{v}.0"
            return f"{v}f"
        return v

    @classmethod
    def _limit_literal(cls, value: str, dt: int) -> str:
        v = value.strip()
        if dt == cls.DT_REAL32:
            v = cls._strip_float_suffix(v)
            if "." not in v and "e" not in v.lower():
                v = f"{v}.0"
            return f"{v}f"
        return v

    def _ordered_cfgs(self, objects: "OrderedDict[int, EdsObject]") -> List[EdsSub]:
        first: List[EdsSub] = []
        normal: List[EdsSub] = []
        for obj in objects.values():
            if obj.object_type == 0x7:
                if 0 not in obj.subs:
                    continue
                sub = obj.subs[0]
                if sub.storage not in ("PERSIST_APP", "ROM"):
                    continue
                (first if sub.enum_first else normal).append(sub)
            elif obj.object_type == 0x8:
                if 1 not in obj.subs:
                    continue
                rep = obj.subs[1]
                (first if rep.enum_first else normal).append(rep)
            elif obj.object_type == 0x9:
                for sub_idx, sub in obj.subs.items():
                    if sub_idx == 0:
                        continue
                    if obj.storage != "PERSIST_APP" and sub.storage not in ("PERSIST_APP", "ROM"):
                        continue
                    (first if sub.enum_first else normal).append(sub)
        first.sort(key=lambda s: (s.index, s.sub))
        normal.sort(key=lambda s: (s.index, s.sub))
        return first + normal

    @staticmethod
    def _is_array(objects: "OrderedDict[int, EdsObject]", sub: EdsSub) -> Tuple[bool, Optional[int]]:
        obj = objects.get(sub.index)
        if obj is None or obj.object_type != 0x8:
            return False, None
        elems = [s for i, s in obj.subs.items() if i != 0]
        return True, len(elems)

    def _gen_bindings(self, objects: "OrderedDict[int, EdsObject]",
                      cfgs: List[EdsSub]) -> str:
        L: List[str] = [
            "/* AUTO-GENERATED by application/scripts/canopen_tools.py — DO NOT EDIT.",
            " *",
            " * Routes manufacturer-block OD reads/writes to ConfigDataId_t entries.",
            " * Source-of-truth: application/canopen/ismart-control-board.xdd (EDS is generated output)",
            " */",
            "#ifndef CONFIG_OD_BINDINGS_INC_",
            "#define CONFIG_OD_BINDINGS_INC_",
            "",
            "#include <stdint.h>",
            "#include <stdbool.h>",
            '#include "storage/config_data.h"',
            "",
            "typedef struct {",
            "\tuint16_t        usIndex;",
            "\tuint8_t         ucSub;",
            "\tConfigDataId_t  xCfgId;",
            "\tbool            bRo;",
            "\tbool            bGated;",
            "} ConfigOdBinding_t;",
            "",
            "static const ConfigOdBinding_t kConfigOdBindings[] = {",
            "",
        ]
        w = L.append

        for obj in objects.values():
            if obj.object_type == 0x7:
                if 0 not in obj.subs:
                    continue
                sub = obj.subs[0]
                ro = "true" if sub.access_type in ("ro", "const") else "false"
                gated = "false" if sub.gated is False else "true"
                w(f"\t{{ 0x{obj.index:04X},   0, eConfigDataId_{sub.name}, {ro}, {gated} }},")
            elif obj.object_type == 0x8:
                elems = [s for i, s in obj.subs.items() if i != 0]
                if not elems:
                    continue
                first_elem = elems[0]
                ro = "true" if first_elem.access_type in ("ro", "const") else "false"
                gated = "false" if first_elem.gated is False else "true"
                cfg_id = f"eConfigDataId_{first_elem.name}"
                for i in range(1, len(elems) + 1):
                    w(f"\t{{ 0x{obj.index:04X}, {i:3d}, {cfg_id}, {ro}, {gated} }},")
            else:
                for sub_idx, sub in obj.subs.items():
                    if sub_idx == 0:
                        continue
                    ro = "true" if sub.access_type in ("ro", "const") else "false"
                    gated = "false" if sub.gated is False else "true"
                    w(f"\t{{ 0x{obj.index:04X}, {sub_idx:3d}, eConfigDataId_{sub.name}, {ro}, {gated} }},")

        L += [
            "};",
            "",
            "static const size_t kConfigOdBindingCount =",
            "\tsizeof(kConfigOdBindings) / sizeof(kConfigOdBindings[0]);",
            "",
            "#endif /* CONFIG_OD_BINDINGS_INC_ */",
            "",
        ]
        return "\n".join(L)

    def _gen_schema_h(self, cfgs: List[EdsSub]) -> str:
        L: List[str] = [
            "/* AUTO-GENERATED by application/scripts/canopen_tools.py — DO NOT EDIT. */",
            "#ifndef CONFIG_DATA_SCHEMA_H_",
            "#define CONFIG_DATA_SCHEMA_H_",
            "",
            "typedef enum {",
        ]
        for i, sub in enumerate(cfgs):
            L.append(f"\tCONFIG_DATA_ID_PREFIX_SETTING({sub.name}) = {i},")
        L += [
            "\t/* Keep this last. */",
            "\tCONFIG_DATA_NUM_OF_ELEMENTS",
            "} ConfigDataId_t;",
            "",
            "extern ConfigData_t xConfigData[CONFIG_DATA_NUM_OF_ELEMENTS];",
            "",
            "#endif /* CONFIG_DATA_SCHEMA_H_ */",
            "",
        ]
        return "\n".join(L)

    def _gen_schema_inc(self, objects: "OrderedDict[int, EdsObject]",
                        cfgs: List[EdsSub]) -> str:
        L: List[str] = [
            "/* AUTO-GENERATED by application/scripts/canopen_tools.py — DO NOT EDIT. */",
            "",
        ]
        w = L.append
        dt0 = self.DT_UNSIGNED32

        for sub in cfgs:
            is_arr, arr_len = self._is_array(objects, sub)
            dt = sub.data_type or dt0
            default = self._c_literal(sub.default or "0", dt)
            lo = self._limit_literal(sub.low_limit or "0", dt)
            hi = self._limit_literal(sub.high_limit or "0", dt)
            if is_arr:
                items = ", ".join([default] * (arr_len or 0))
                if dt == self.DT_REAL32:
                    w(f"CONFIG_DATA_FLOAT_ARR_DEFINE({sub.name}, {lo}, {hi}, {items});")
                else:
                    w(f"CONFIG_DATA_INT_ARR_DEFINE({sub.name}, {lo}, {hi}, {items});")
            elif dt == self.DT_VISIBLE_STRING:
                w(f"CONFIG_DATA_STR_DEFINE({sub.name}, {default});")
            elif dt == self.DT_REAL32:
                w(f"CONFIG_DATA_FLOAT_DEFINE({sub.name}, {default}, {lo}, {hi});")
            else:
                w(f"CONFIG_DATA_INT_DEFINE({sub.name}, {default}, {lo}, {hi});")

        w("")
        w("ConfigData_t xConfigData[CONFIG_DATA_NUM_OF_ELEMENTS] = {")
        for sub in cfgs:
            is_arr, _ = self._is_array(objects, sub)
            dt = sub.data_type or dt0
            if is_arr:
                if dt == self.DT_REAL32:
                    w(f"\tCONFIG_DATA_FLOAT_ARR_ENTRY({sub.name}),")
                else:
                    w(f"\tCONFIG_DATA_INT_ARR_ENTRY({sub.name}),")
            elif dt == self.DT_VISIBLE_STRING:
                w(f"\tCONFIG_DATA_STR_ENTRY({sub.name}),")
            elif dt == self.DT_REAL32:
                w(f"\tCONFIG_DATA_FLOAT_ENTRY({sub.name}),")
            else:
                w(f"\tCONFIG_DATA_INT_ENTRY({sub.name}),")
        w("};")
        w("")
        w("void vConfigDataResetDefault(void)")
        w("{")
        for sub in cfgs:
            is_arr, _ = self._is_array(objects, sub)
            dt = sub.data_type or dt0
            if is_arr or dt == self.DT_VISIBLE_STRING:
                w(f"\tCONFIG_DATA_RESET_ARR_DEFAULT({sub.name});")
            else:
                w(f"\tCONFIG_DATA_RESET_NUM_DEFAULT({sub.name});")
        w("}")
        w("")
        return "\n".join(L)

    def generate(self, eds_path: Path, repo_root: Path,
                 check_only: bool = False) -> bool:
        """Generate config C artifacts. Returns True if files were/would-be changed."""
        objects = EdsParser().parse(eds_path, index_range=(0x2200, 0x22FF))
        cfgs = self._ordered_cfgs(objects)

        out_bindings = repo_root / "application/source/storage/config_od_bindings.inc"
        out_schema_h = repo_root / "application/source/include/storage/config_data_schema.h"
        out_schema_inc = repo_root / "application/source/storage/config_data_schema.inc"

        texts = {
            out_bindings: self._gen_bindings(objects, cfgs),
            out_schema_h: self._gen_schema_h(cfgs),
            out_schema_inc: self._gen_schema_inc(objects, cfgs),
        }

        changed = any(
            not p.exists() or p.read_text(encoding="utf-8", errors="replace") != t
            for p, t in texts.items()
        )

        if check_only:
            return changed

        for p, t in texts.items():
            p.parent.mkdir(parents=True, exist_ok=True)
            old = p.read_text(encoding="utf-8", errors="replace") if p.exists() else None
            if old != t:
                p.write_text(t, encoding="utf-8")
                print(f"  wrote {p.relative_to(repo_root)}")
            else:
                print(f"  unchanged {p.relative_to(repo_root)}")

        return changed


# ===========================================================================
# EdsDescPatcher  — insert ;Description= annotations into EDS
# ===========================================================================

class EdsDescPatcher:
    """Insert ;Description= annotations into EDS manufacturer objects."""

    DESCRIPTIONS: Dict[str, str] = {
        "2000": "Real-time regulation mode and inverter command flags",
        "2000sub1": "Regulation mode: 0=open-loop frequency, 1=closed-loop power",
        "2000sub2": "Inverter command flags bitmask (bit 0=HEAT_ENABLE, bit 1=FAULT_CLEAR)",
        "2001": "Process setpoint and operating frequency command",
        "2001sub1": "Regulation process setpoint (W in power mode, Hz in frequency mode)",
        "2001sub2": "Operating frequency command in kHz (1.0 = 1 kHz; used in open-loop frequency mode)",
        "2010": "Real-time system status flag words",
        "2010sub1": "General system status flags bitmask",
        "2010sub2": "Active operating limit flags bitmask",
        "2010sub3": "Active fault flags bitmask; non-zero when a fault is asserted",
        "2011": "Primary inverter electrical feedback: coil current and output power",
        "2011sub1": "Measured inverter coil current in amperes (REAL32)",
        "2011sub2": "Calculated output power in watts (REAL32)",
        "2012": "Secondary process feedback: operating frequency and accumulated energy",
        "2012sub1": "Measured operating frequency in kHz (REAL32)",
        "2012sub2": "Accumulated energy counter in Wh (UNSIGNED32)",
        "2013": "Coil and load characterisation feedback",
        "2013sub1": "Coil signature word encoding load impedance and resonant tuning state",
        "2100": "Persistent configuration lifecycle commands: save, apply, reset",
        "2100sub1": "Write 1 to persist current configuration to non-volatile flash",
        "2100sub2": "Write 1 to apply pending configuration changes at runtime",
        "2100sub3": "Write 1 to restore all configuration entries to factory defaults",
        "2200": "Energy management mode: 0=disabled, 1=enabled (energy-aware)",
        "2210": "Analog power/current input reference conditioning parameters",
        "2210sub1": "Analog input reference type: 0=voltage, 1=current",
        "2210sub2": "Maximum expected analog voltage reference level in millivolts",
        "2210sub3": "Maximum expected analog current reference level in millivolts",
        "2210sub4": "Analog zero-point trim offset in millivolts",
        "2220": "Inverter ADC calibration: current zero offset, gain, voltage full-scale",
        "2220sub1": "ADC zero-current baseline offset in LSB counts",
        "2220sub2": "Inverter current ADC gain scaling factor in mA per LSB",
        "2220sub3": "Inverter voltage ADC full-scale reference level in millivolts",
        "2230": "Current transformer (CT) ADC calibration gains and input mode",
        "2230sub1": "CT channel 1 gain scaling factor in mA per LSB",
        "2230sub2": "CT channel 2 gain scaling factor in mA per LSB",
        "2230sub3": "CT input mode: 0=single CT1, 1=dual CT averaging CT1+CT2",
        "2240": "DC bus voltage ADC full-scale reference level in millivolts",
        "2250": "IFP power-regulation PID controller tuning parameters",
        "2250sub1": "Power PID feature flags bitmask",
        "2250sub2": "Power PID proportional gain coefficient Kc",
        "2250sub3": "Power PID integral time constant Ti in seconds",
        "2250sub4": "Power PID derivative time constant Td in seconds",
        "2250sub5": "Power PID output ramp rate limit",
        "2251": "IFP current-regulation PID controller tuning parameters",
        "2251sub1": "Current PID feature flags bitmask",
        "2251sub2": "Current PID proportional gain Kc",
        "2251sub3": "Current PID integral time constant Ti in seconds",
        "2251sub4": "Current PID derivative time constant Td in seconds",
        "2251sub5": "Current PID output ramp rate limit",
        "2252": "IFP voltage-regulation PID controller tuning parameters",
        "2252sub1": "Voltage PID feature flags bitmask",
        "2252sub2": "Voltage PID proportional gain Kc",
        "2252sub3": "Voltage PID integral time constant Ti in seconds",
        "2252sub4": "Voltage PID derivative time constant Td in seconds",
        "2252sub5": "Voltage PID output ramp rate limit",
        "2253": "IFP operating frequency range and sweep slew-rate limits",
        "2253sub1": "Minimum allowed operating frequency in Hz",
        "2253sub2": "Maximum allowed operating frequency in Hz",
        "2253sub3": "Frequency sweep step increment in Hz per control tick",
        "2254": "101-entry normalised current limit lookup table (0=min_freq, 100=max_freq)",
        "2255": "101-entry adaptive proportional gain lookup table indexed by operating point",
        "2280": "Configuration schema version string (read-only)",
    }

    def patch(self, eds_path: Path) -> int:
        """Patch ;Description= lines into eds_path in-place. Returns count inserted."""
        original = eds_path.read_text(encoding="utf-8", errors="replace")
        stripped = re.sub(r"^;Description=.*\n", "", original, flags=re.MULTILINE)
        lines = stripped.splitlines(keepends=True)
        out: List[str] = []
        pending: Optional[str] = None
        count = 0
        for line in lines:
            m = re.match(r"^\[([^\]]+)\]\s*$", line.rstrip("\n"))
            if m:
                pending = self.DESCRIPTIONS.get(m.group(1))
                out.append(line)
                continue
            if pending is not None and re.match(r"^ParameterName=", line.strip()):
                out.append(line)
                out.append(f";Description={pending}\n")
                pending = None
                count += 1
                continue
            if pending is not None and line.strip() and not line.strip().startswith(";"):
                pending = None
            out.append(line)
        eds_path.write_text("".join(out), encoding="utf-8")
        return count


# ===========================================================================
# CLI
# ===========================================================================

def _require_file(path: Optional[Path], label: str) -> Path:
    if path is None or not path.exists():
        print(f"ERROR: {label} not found: {path}", file=sys.stderr)
        sys.exit(1)
    return path


def cmd_xdd2od(args: argparse.Namespace) -> int:
    xdd = Path(args.xdd) if args.xdd else _DEFAULT_XDD
    _require_file(xdd, "XDD")
    out_dir = Path(args.outdir) if args.outdir else Path(".")
    c, h = OdGenerator().generate(xdd, out_dir)
    print(f"  OD.h -> {h}")
    print(f"  OD.c -> {c}")
    return 0


def cmd_xdd2eds(args: argparse.Namespace) -> int:
    xdd = Path(args.xdd) if args.xdd else _DEFAULT_XDD
    _require_file(xdd, "XDD")
    out = Path(args.out) if args.out else xdd.with_suffix(".eds")
    EdsGenerator().generate(xdd, out)
    return 0


def cmd_eds2config(args: argparse.Namespace) -> int:
    eds = Path(args.eds) if args.eds else _DEFAULT_EDS
    _require_file(eds, "EDS")
    repo = Path(args.repo) if args.repo else _DEFAULT_REPO
    gen = ConfigGenerator()
    changed = gen.generate(eds, repo, check_only=args.check)
    if args.check and changed:
        print("eds2config: outputs are stale; re-run canopen_tools.py eds2config",
              file=sys.stderr)
        return 1
    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    path = Path(args.infile) if args.infile else _DEFAULT_EDS
    _require_file(path, "input file")
    suffix = path.suffix.lower()
    if suffix == ".xdd":
        try:
            objs = XddParser().parse(path)
            print(f"XDD valid: {len(objs)} objects parsed from {path.name}")
        except Exception as exc:
            print(f"XDD ERROR: {exc}", file=sys.stderr)
            return 1
    else:
        errors = EdsParser().validate(path)
        if errors:
            for e in errors:
                print(f"  {e}", file=sys.stderr)
            print(f"EDS INVALID: {len(errors)} error(s) in {path.name}", file=sys.stderr)
            return 1
        # Also attempt full parse
        try:
            objs = EdsParser().parse(path)
            print(f"EDS valid: {len(objs)} objects parsed from {path.name}")
        except Exception as exc:
            print(f"EDS parse ERROR: {exc}", file=sys.stderr)
            return 1
    return 0


def cmd_patch_desc(args: argparse.Namespace) -> int:
    eds = Path(args.eds) if args.eds else _DEFAULT_EDS
    _require_file(eds, "EDS")
    count = EdsDescPatcher().patch(eds)
    print(f"Inserted {count} ;Description= annotations -> {eds}")
    return 0


def _compat_mode(argv: List[str]) -> int:
    """eds2c_wrapper.py drop-in: <eds_or_xdd_file> -o <outdir>"""
    infile = Path(argv[0])
    out_dir = Path(".")
    if "-o" in argv:
        idx = argv.index("-o")
        if idx + 1 < len(argv):
            out_dir = Path(argv[idx + 1])

    # Prefer XDD alongside the given file
    xdd = infile.with_suffix(".xdd")
    if not xdd.exists() and infile.suffix.lower() == ".xdd":
        xdd = infile
    if not xdd.exists():
        # Try same directory for the default XDD name
        xdd = infile.parent / _DEFAULT_XDD.name
    if not xdd.exists():
        print(f"ERROR: cannot find XDD for {infile}", file=sys.stderr)
        return 1

    print(f"[canopen_tools compat] xdd2od {xdd.name} -> {out_dir}")
    c, h = OdGenerator().generate(xdd, out_dir)
    print(f"  OD.h -> {h}")
    print(f"  OD.c -> {c}")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="canopen_tools.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="command", metavar="<command>")

    # xdd2od
    p1 = sub.add_parser("xdd2od",
        help="Generate OD.c + OD.h from XDD (CANopenNode V4)")
    p1.add_argument("--xdd", metavar="PATH",
        help=f"Input XDD file [default: {_DEFAULT_XDD.name}]")
    p1.add_argument("--outdir", metavar="DIR",
        help="Output directory [default: .]")

    # xdd2eds
    p2 = sub.add_parser("xdd2eds", help="Generate EDS from XDD")
    p2.add_argument("--xdd", metavar="PATH",
        help=f"Input XDD file [default: {_DEFAULT_XDD.name}]")
    p2.add_argument("--out", metavar="PATH",
        help="Output EDS file [default: <xdd_stem>.eds]")

    # eds2config
    p3 = sub.add_parser("eds2config",
        help="Generate firmware config-layer C artifacts from EDS")
    p3.add_argument("--eds", metavar="PATH",
        help=f"Input EDS file [default: {_DEFAULT_EDS.name}]")
    p3.add_argument("--repo", metavar="PATH",
        help="Repo root [default: auto-discover]")
    p3.add_argument("--check", action="store_true",
        help="Exit 1 if outputs would change (no files written)")

    # validate
    p4 = sub.add_parser("validate", help="Validate an EDS or XDD file")
    p4.add_argument("--infile", metavar="PATH",
        help=f"Input EDS or XDD [default: {_DEFAULT_EDS.name}]")

    # patch-desc
    p5 = sub.add_parser("patch-desc",
        help="Insert ;Description= annotations into EDS")
    p5.add_argument("--eds", metavar="PATH",
        help=f"EDS file to patch in-place [default: {_DEFAULT_EDS.name}]")

    return p


def main(argv: Optional[List[str]] = None) -> int:
    if argv is None:
        argv = sys.argv[1:]

    # eds2c_wrapper.py compatibility: first arg is a file path
    if argv and not argv[0].startswith("-") and argv[0] not in (
            "xdd2od", "xdd2eds", "eds2config", "validate", "patch-desc"):
        return _compat_mode(argv)

    parser = _build_parser()
    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 0

    dispatch = {
        "xdd2od":    cmd_xdd2od,
        "xdd2eds":   cmd_xdd2eds,
        "eds2config": cmd_eds2config,
        "validate":  cmd_validate,
        "patch-desc": cmd_patch_desc,
    }
    try:
        return dispatch[args.command](args)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
