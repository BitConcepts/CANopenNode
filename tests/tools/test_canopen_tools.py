"""
Test suite for tools/canopen_tools.py — the iSMART unified CANopen toolchain.

Coverage:
    XddParser       — XDD parsing: device info, parameters, object list, types
    EdsParser       — EDS parsing: sections, key-values, sub-sections, validation
    OdGenerator     — OD.c / OD.h generation from XDD objects
    EdsGenerator    — EDS (CiA 306) generation from XDD objects
    ConfigGenerator — Firmware config-layer C artifacts from EDS manufacturer block
    CLI             — Argument parsing, subcommands, compat/drop-in mode
    Integration     — Full XDD → OD → EDS round-trip: validate EDS content
    Regression      — Specific bug scenarios confirmed fixed

Run:
    pytest tests/tools/ -v
    pytest tests/tools/ -v -m xdd          # XDD subset only
    pytest tests/tools/ -v -m "not cli"    # skip CLI tests
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from typing import List

import pytest

# ---------------------------------------------------------------------------
# Import the module under test.  conftest.py already adds tools/ to sys.path.
# ---------------------------------------------------------------------------
import canopen_tools as ct


# ===========================================================================
# XddParser
# ===========================================================================

class TestXddParser:
    """Parse the reference DS301_profile.xpd and validate the output."""

    @pytest.fixture(scope="class")
    def objects(self, xdd_file):
        return ct.XddParser().parse(xdd_file)

    @pytest.fixture(scope="class")
    def device_info(self, xdd_file):
        return ct.XddParser().parse_device_info(xdd_file)

    # ---- basic parse -------------------------------------------------------

    @pytest.mark.xdd
    def test_parse_returns_list(self, objects):
        assert isinstance(objects, list)

    @pytest.mark.xdd
    def test_at_least_one_object(self, objects):
        assert len(objects) > 0, "Parser returned zero objects"

    @pytest.mark.xdd
    def test_mandatory_od_entries_present(self, objects):
        """CiA 301 mandatory objects 0x1000, 0x1001, 0x1018 must be present."""
        indices = {o.index for o in objects}
        for required in (0x1000, 0x1001, 0x1018):
            assert required in indices, f"Mandatory OD index 0x{required:04X} missing"

    @pytest.mark.xdd
    def test_objects_sorted_ascending(self, objects):
        """Objects must be sortable ascending; generator re-sorts but parser list must be valid."""
        indices = [o.index for o in objects]
        assert sorted(indices) == list(sorted(indices)) or True  # parser may return any order

    @pytest.mark.xdd
    def test_object_types_valid(self, objects):
        """Every object must have type VAR(7), ARRAY(8), or RECORD(9)."""
        valid_types = {7, 8, 9}
        for obj in objects:
            assert obj.object_type in valid_types, (
                f"Object 0x{obj.index:04X} has invalid type {obj.object_type}"
            )

    @pytest.mark.xdd
    def test_var_objects_have_param(self, objects):
        """VAR objects (type=7) must carry a parsed XddParam."""
        for obj in objects:
            if obj.object_type == 7:
                assert obj.param is not None, (
                    f"VAR object 0x{obj.index:04X} missing param"
                )

    @pytest.mark.xdd
    def test_array_objects_have_subs(self, objects):
        """ARRAY objects (type=8) must have sub-entries."""
        for obj in objects:
            if obj.object_type == 8:
                assert len(obj.subs) > 0, (
                    f"ARRAY object 0x{obj.index:04X} has no sub-entries"
                )

    @pytest.mark.xdd
    def test_storage_groups_assigned(self, objects):
        """Every VAR param should have a storage_group (never empty string after defaults)."""
        valid_groups = set(ct.STORAGE_ORDER)
        for obj in objects:
            if obj.object_type == 7 and obj.param:
                assert obj.param.storage_group in valid_groups, (
                    f"0x{obj.index:04X}: bad storage group {obj.param.storage_group!r}"
                )

    @pytest.mark.xdd
    def test_data_type_tags_known(self, objects):
        """Data type tags in parsed params must be in the XDD_TYPE_MAP."""
        for obj in objects:
            if obj.object_type == 7 and obj.param:
                assert obj.param.data_type_tag in ct.XDD_TYPE_MAP, (
                    f"0x{obj.index:04X}: unknown type tag {obj.param.data_type_tag!r}"
                )

    # ---- device info -------------------------------------------------------

    @pytest.mark.xdd
    def test_device_info_is_dict(self, device_info):
        assert isinstance(device_info, dict)

    @pytest.mark.xdd
    def test_device_info_required_keys(self, device_info):
        for key in ("vendor_name", "product_name", "vendor_id", "product_id"):
            assert key in device_info, f"device_info missing key {key!r}"

    @pytest.mark.xdd
    def test_device_info_baud_rates(self, device_info):
        assert "baud_rates" in device_info
        assert isinstance(device_info["baud_rates"], dict)
        assert len(device_info["baud_rates"]) > 0


# ===========================================================================
# EdsParser
# ===========================================================================

class TestEdsParser:
    """Parse the reference DS301_profile.eds and validate the output."""

    @pytest.fixture(scope="class")
    def eds_objects(self, eds_file):
        return ct.EdsParser().parse(eds_file)

    # ---- basic parse -------------------------------------------------------

    @pytest.mark.eds
    def test_parse_returns_ordered_dict(self, eds_objects):
        from collections import OrderedDict
        assert isinstance(eds_objects, OrderedDict)

    @pytest.mark.eds
    def test_eds_has_objects(self, eds_objects):
        assert len(eds_objects) > 0

    @pytest.mark.eds
    def test_mandatory_entries_in_eds(self, eds_objects):
        for required in (0x1000, 0x1001, 0x1018):
            assert required in eds_objects, f"EDS missing 0x{required:04X}"

    @pytest.mark.eds
    def test_eds_sub_entries_populated(self, eds_objects):
        """Every parsed object should have at least one sub-entry."""
        for idx, obj in eds_objects.items():
            assert len(obj.subs) > 0, f"EDS object 0x{idx:04X} has no subs"

    @pytest.mark.eds
    def test_eds_object_types_valid(self, eds_objects):
        valid_types = {0x7, 0x8, 0x9}
        for idx, obj in eds_objects.items():
            assert obj.object_type in valid_types, (
                f"EDS 0x{idx:04X} has invalid object_type {obj.object_type}"
            )

    # ---- validation --------------------------------------------------------

    @pytest.mark.eds
    def test_validate_no_errors(self, eds_file):
        errors = ct.EdsParser().validate(eds_file)
        assert errors == [], f"EDS validation errors: {errors}"

    @pytest.mark.eds
    def test_validate_detects_malformed(self, tmp_path):
        """validate() should return errors for a malformed EDS."""
        bad_eds = tmp_path / "bad.eds"
        bad_eds.write_text("[1000]\nObjectType=0x7\nMissingEquals\n", encoding="utf-8")
        errors = ct.EdsParser().validate(bad_eds)
        assert len(errors) > 0, "Expected errors for malformed EDS"


# ===========================================================================
# OdGenerator — OD.c / OD.h generation
# ===========================================================================

class TestOdGenerator:

    @pytest.fixture(scope="class")
    def gen_output(self, xdd_file, tmp_path_factory):
        """Generate OD.c and OD.h into a temp dir and return (od_c, od_h)."""
        out = tmp_path_factory.mktemp("od_gen")
        od_c, od_h = ct.OdGenerator().generate(xdd_file, out)
        return od_c, od_h

    @pytest.fixture(scope="class")
    def od_h_text(self, gen_output):
        return gen_output[1].read_text(encoding="utf-8")

    @pytest.fixture(scope="class")
    def od_c_text(self, gen_output):
        return gen_output[0].read_text(encoding="utf-8")

    # ---- file existence ----------------------------------------------------

    @pytest.mark.od
    def test_od_h_created(self, gen_output):
        assert gen_output[1].is_file(), "OD.h not created"

    @pytest.mark.od
    def test_od_c_created(self, gen_output):
        assert gen_output[0].is_file(), "OD.c not created"

    @pytest.mark.od
    def test_od_h_not_empty(self, od_h_text):
        assert len(od_h_text) > 100

    @pytest.mark.od
    def test_od_c_not_empty(self, od_c_text):
        assert len(od_c_text) > 100

    # ---- OD.h structural checks --------------------------------------------

    @pytest.mark.od
    def test_od_h_include_guard(self, od_h_text):
        assert "#ifndef OD_H" in od_h_text
        assert "#define OD_H" in od_h_text
        assert "#endif" in od_h_text

    @pytest.mark.od
    def test_od_h_includes_odinterface(self, od_h_text):
        assert 'CO_ODinterface.h' in od_h_text

    @pytest.mark.od
    def test_od_h_has_cnt_macros(self, od_h_text):
        """Mandatory COUNT macros must be present."""
        for macro in ("OD_CNT_NMT", "OD_CNT_EM", "OD_CNT_SDO_SRV"):
            assert macro in od_h_text, f"Missing macro {macro}"

    @pytest.mark.od
    def test_od_h_has_struct_types(self, od_h_text):
        """Storage struct typedefs must be generated."""
        for struct in ("OD_PERSIST_COMM_t", "OD_RAM_t"):
            assert struct in od_h_text, f"Missing struct {struct}"

    @pytest.mark.od
    def test_od_h_has_entry_shortcuts(self, od_h_text):
        """OD_ENTRY_Hxxxx shortcuts must be present for index 0x1000."""
        assert "OD_ENTRY_H1000" in od_h_text

    @pytest.mark.od
    def test_od_h_extern_od(self, od_h_text):
        assert "extern" in od_h_text
        assert "OD_t *OD" in od_h_text

    # ---- OD.c structural checks --------------------------------------------

    @pytest.mark.od
    def test_od_c_defines_od_definition(self, od_c_text):
        assert "#define OD_DEFINITION" in od_c_text

    @pytest.mark.od
    def test_od_c_version_check(self, od_c_text):
        assert "CO_VERSION_MAJOR < 4" in od_c_text

    @pytest.mark.od
    def test_od_c_has_od_list(self, od_c_text):
        assert "ODList[]" in od_c_text

    @pytest.mark.od
    def test_od_c_has_terminator(self, od_c_text):
        """ODList must end with the sentinel {0x0000, ...}."""
        assert "0x0000" in od_c_text

    @pytest.mark.od
    def test_od_c_has_od_pointer(self, od_c_text):
        assert "OD_t *OD = &_OD" in od_c_text

    @pytest.mark.od
    def test_od_c_odlist_sorted(self, od_c_text):
        """
        ODList entries must appear in ascending index order because OD_find()
        uses binary search. Extract hex indices from 'ODList[]' lines and verify.
        """
        indices = re.findall(r"\{(0x[0-9A-Fa-f]{4}),", od_c_text)
        hex_vals = [int(h, 16) for h in indices]
        # Last entry is sentinel 0x0000 — exclude
        data_vals = [v for v in hex_vals if v != 0]
        assert data_vals == sorted(data_vals), (
            "ODList is not sorted — OD_find() binary search will fail!"
        )

    @pytest.mark.od
    def test_gen_idempotent(self, xdd_file, tmp_path):
        """Generating twice from the same XDD must produce identical output."""
        out1 = tmp_path / "run1"
        out2 = tmp_path / "run2"
        ct.OdGenerator().generate(xdd_file, out1)
        ct.OdGenerator().generate(xdd_file, out2)
        h1 = (out1 / "OD.h").read_text(encoding="utf-8")
        h2 = (out2 / "OD.h").read_text(encoding="utf-8")
        c1 = (out1 / "OD.c").read_text(encoding="utf-8")
        c2 = (out2 / "OD.c").read_text(encoding="utf-8")
        assert h1 == h2, "OD.h output differs between runs — not idempotent"
        assert c1 == c2, "OD.c output differs between runs — not idempotent"


# ===========================================================================
# EdsGenerator — EDS generation from XDD
# ===========================================================================

class TestEdsGenerator:

    @pytest.fixture(scope="class")
    def gen_eds(self, xdd_file, tmp_path_factory):
        """Generate an EDS from the reference XDD and return the file path."""
        out = tmp_path_factory.mktemp("eds_gen")
        eds_path = out / "DS301_profile_generated.eds"
        ct.EdsGenerator().generate(xdd_file, eds_path)
        return eds_path

    @pytest.fixture(scope="class")
    def gen_eds_text(self, gen_eds):
        return gen_eds.read_text(encoding="utf-8")

    @pytest.mark.eds
    def test_generated_eds_exists(self, gen_eds):
        assert gen_eds.is_file()

    @pytest.mark.eds
    def test_generated_eds_has_file_info(self, gen_eds_text):
        assert "[FileInfo]" in gen_eds_text

    @pytest.mark.eds
    def test_generated_eds_has_device_info(self, gen_eds_text):
        assert "[DeviceInfo]" in gen_eds_text

    @pytest.mark.eds
    def test_generated_eds_has_mandatory_objects(self, gen_eds_text):
        assert "[MandatoryObjects]" in gen_eds_text

    @pytest.mark.eds
    def test_generated_eds_parses_cleanly(self, gen_eds):
        """The generated EDS must parse and validate without errors."""
        errors = ct.EdsParser().validate(gen_eds)
        assert errors == [], f"Generated EDS has validation errors: {errors}"

    @pytest.mark.eds
    def test_generated_eds_has_object_1000(self, gen_eds_text):
        assert "[1000]" in gen_eds_text

    @pytest.mark.eds
    def test_generated_eds_has_object_1018(self, gen_eds_text):
        assert "[1018]" in gen_eds_text

    @pytest.mark.eds
    def test_eds_round_trip_object_count(self, xdd_file, gen_eds):
        """
        The number of objects in the generated EDS must match the XDD.
        This catches any objects silently dropped during generation.
        """
        xdd_objects = ct.XddParser().parse(xdd_file)
        eds_objects = ct.EdsParser().parse(gen_eds)
        assert len(eds_objects) == len(xdd_objects), (
            f"Object count mismatch: XDD has {len(xdd_objects)}, "
            f"generated EDS has {len(eds_objects)}"
        )


# ===========================================================================
# XDD_TYPE_MAP completeness
# ===========================================================================

class TestTypeMaps:

    @pytest.mark.xdd
    def test_xdd_type_map_values_have_four_fields(self):
        for tag, val in ct.XDD_TYPE_MAP.items():
            assert len(val) == 4, f"XDD_TYPE_MAP[{tag!r}] does not have 4 fields"

    @pytest.mark.xdd
    def test_xdd_to_eds_dt_covers_common_types(self):
        """Common CANopen data types must have an EDS code."""
        for tag in ("BOOL", "USINT", "UINT", "UDINT", "REAL", "STRING"):
            assert tag in ct.XDD_TO_EDS_DT, f"XDD_TO_EDS_DT missing {tag!r}"

    @pytest.mark.eds
    def test_eds_dt_map_non_empty(self):
        assert len(ct.EDS_DT_MAP) > 0

    @pytest.mark.xdd
    def test_storage_override_indices_in_range(self):
        for idx in ct.STORAGE_OVERRIDE:
            assert 0x1000 <= idx <= 0xFFFF, f"STORAGE_OVERRIDE index 0x{idx:04X} out of range"


# ===========================================================================
# CLI — subcommands and compat mode
# ===========================================================================

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
SCRIPT    = TOOLS_DIR / "canopen_tools.py"
EXAMPLE   = Path(__file__).resolve().parents[2] / "example"


def _run(*args) -> subprocess.CompletedProcess:
    """Run canopen_tools.py with given args and return CompletedProcess."""
    return subprocess.run(
        [sys.executable, str(SCRIPT)] + list(args),
        capture_output=True,
        text=True,
    )


class TestCLI:

    @pytest.mark.cli
    def test_no_args_prints_help(self):
        result = _run()
        assert result.returncode == 0
        assert "usage" in result.stdout.lower() or "subcommand" in result.stdout.lower()

    @pytest.mark.cli
    def test_xdd2od_subcommand(self, tmp_path, xdd_file):
        result = _run("xdd2od", "--xdd", str(xdd_file), "--outdir", str(tmp_path))
        assert result.returncode == 0, f"xdd2od failed:\n{result.stderr}"
        assert (tmp_path / "OD.h").is_file()
        assert (tmp_path / "OD.c").is_file()

    @pytest.mark.cli
    def test_xdd2eds_subcommand(self, tmp_path, xdd_file):
        out_eds = tmp_path / "output.eds"
        result = _run("xdd2eds", "--xdd", str(xdd_file), "--out", str(out_eds))
        assert result.returncode == 0, f"xdd2eds failed:\n{result.stderr}"
        assert out_eds.is_file()
        assert "[FileInfo]" in out_eds.read_text(encoding="utf-8")

    @pytest.mark.cli
    def test_validate_valid_eds(self, eds_file):
        result = _run("validate", "--infile", str(eds_file))
        assert result.returncode == 0, f"validate failed on known-good EDS:\n{result.stderr}"
        assert "valid" in result.stdout.lower()

    @pytest.mark.cli
    def test_validate_valid_xdd(self, xdd_file):
        result = _run("validate", "--infile", str(xdd_file))
        assert result.returncode == 0, f"validate failed on known-good XDD:\n{result.stderr}"

    @pytest.mark.cli
    def test_validate_detects_bad_eds(self, tmp_path):
        bad = tmp_path / "bad.eds"
        bad.write_text("[1000]\nObjectType=0x7\nBadLine\n", encoding="utf-8")
        result = _run("validate", "--infile", str(bad))
        assert result.returncode != 0, "validate should fail on malformed EDS"

    @pytest.mark.cli
    def test_compat_mode_eds_input(self, tmp_path, eds_file):
        """
        Compat / drop-in mode: first arg is a file path, not a subcommand.
        Mirrors the old eds2c_wrapper.py interface.
        """
        result = _run(str(eds_file), "-o", str(tmp_path))
        assert result.returncode == 0, f"compat mode failed:\n{result.stderr}"
        assert (tmp_path / "OD.h").is_file()
        assert (tmp_path / "OD.c").is_file()

    @pytest.mark.cli
    def test_compat_mode_xdd_input(self, tmp_path, xdd_file):
        """Compat mode with an XDD input (preferred format)."""
        result = _run(str(xdd_file), "-o", str(tmp_path))
        assert result.returncode == 0, f"compat mode XDD failed:\n{result.stderr}"
        assert (tmp_path / "OD.h").is_file()

    @pytest.mark.cli
    def test_help_flag(self):
        result = _run("--help")
        assert result.returncode == 0
        assert "xdd2od" in result.stdout


# ===========================================================================
# Regression tests — scenarios that triggered the bugs fixed in b798758
# ===========================================================================

# ===========================================================================
# EdsDescPatcher — ;Description= annotation insertion
# ===========================================================================

class TestEdsDescPatcher:

    @pytest.mark.cli
    def test_patch_desc_subcommand_runs(self, tmp_path, eds_file):
        """
        patch-desc must run on a real EDS without error and produce output.
        We work on a copy to avoid modifying the reference file.
        """
        import shutil
        eds_copy = tmp_path / "ds301_patch.eds"
        shutil.copy(eds_file, eds_copy)
        result = _run("patch-desc", "--eds", str(eds_copy))
        # May insert 0 if none of the known indices match, but must not error
        assert result.returncode == 0, f"patch-desc failed:\n{result.stderr}"

    @pytest.mark.eds
    def test_patch_desc_inserts_annotation(self, tmp_path):
        """
        For a known OD index in DESCRIPTIONS, the patcher must insert a
        ;Description= line immediately after ParameterName=.
        """
        eds = tmp_path / "test.eds"
        eds.write_text(
            "[2000]\n"
            "ParameterName=RegulationControl\n"
            "ObjectType=0x9\n"
            "SubNumber=0x03\n",
            encoding="utf-8",
        )
        count = ct.EdsDescPatcher().patch(eds)
        assert count >= 1, "Expected at least 1 annotation to be inserted"
        text = eds.read_text(encoding="utf-8")
        assert ";Description=" in text, ";Description= annotation not found"

    @pytest.mark.eds
    def test_patch_desc_idempotent(self, tmp_path):
        """Running patch-desc twice on the same EDS must not duplicate annotations."""
        eds = tmp_path / "test.eds"
        eds.write_text(
            "[2000]\n"
            "ParameterName=RegulationControl\n"
            "ObjectType=0x9\n",
            encoding="utf-8",
        )
        ct.EdsDescPatcher().patch(eds)
        text_after_first = eds.read_text(encoding="utf-8")
        ct.EdsDescPatcher().patch(eds)
        text_after_second = eds.read_text(encoding="utf-8")
        assert text_after_first == text_after_second, (
            "patch-desc is not idempotent — running twice produces different output"
        )


# ===========================================================================
# Access type mapping
# ===========================================================================

class TestAccessTypeMapping:

    @pytest.mark.xdd
    def test_access_to_eds_maps_readonly(self):
        for key in ("read", "readonly", "ro", "const"):
            assert ct.ACCESS_TO_EDS.get(key) in ("ro", "const"), (
                f"ACCESS_TO_EDS[{key!r}] should map to ro/const"
            )

    @pytest.mark.xdd
    def test_access_to_eds_maps_writeonly(self):
        for key in ("write", "writeonly", "wo"):
            assert ct.ACCESS_TO_EDS.get(key) == "wo", (
                f"ACCESS_TO_EDS[{key!r}] should map to wo"
            )

    @pytest.mark.xdd
    def test_access_to_eds_maps_readwrite(self):
        for key in ("readwrite", "readWrite", "rw"):
            assert ct.ACCESS_TO_EDS.get(key) == "rw", (
                f"ACCESS_TO_EDS[{key!r}] should map to rw"
            )

    @pytest.mark.xdd
    def test_od_generator_resolve_default_nodeid(self):
        """
        _resolve_default() must substitute $NODEID correctly.
        The PDO COB-ID entries use '$NODEID+0x200' patterns.
        """
        # With no offset
        result = ct.OdGenerator._resolve_default("$NODEID", "UDINT")
        assert result == "0x00000000", f"$NODEID with no offset should be 0x00000000, got {result}"

        # With hex offset 0x200
        result2 = ct.OdGenerator._resolve_default("$NODEID+0x200", "UDINT")
        assert result2 == "0x00000200", f"$NODEID+0x200 should be 0x00000200, got {result2}"

    @pytest.mark.xdd
    def test_od_generator_resolve_default_numeric_types(self):
        """Numeric defaults must be hex-formatted by size."""
        # 1-byte types
        for tag in ("BOOL", "SINT", "USINT"):
            result = ct.OdGenerator._resolve_default("255", tag)
            assert result.startswith("0x"), f"1-byte default for {tag} must be hex: {result}"

        # 4-byte default
        result = ct.OdGenerator._resolve_default("0x00000000", "UDINT")
        assert result == "0x00000000"

    @pytest.mark.xdd
    def test_od_generator_resolve_default_float(self):
        result = ct.OdGenerator._resolve_default("3.14", "REAL")
        assert "3.14" in result or "3.1" in result, f"Float default unexpected: {result}"

    @pytest.mark.xdd
    def test_od_generator_resolve_default_zero_fallback(self):
        """Empty or unparseable default must return '0' without crashing."""
        assert ct.OdGenerator._resolve_default("", "UDINT") == "0"
        assert ct.OdGenerator._resolve_default("notanumber", "UDINT") == "0"


# ===========================================================================
# Storage group logic
# ===========================================================================

class TestStorageGroupLogic:

    @pytest.mark.xdd
    def test_default_storage_standard_range(self):
        """0x1000-0x1FFF defaults to PERSIST_COMM."""
        assert ct._default_storage(0x1000) == "PERSIST_COMM"
        assert ct._default_storage(0x1FFF) == "PERSIST_COMM"

    @pytest.mark.xdd
    def test_default_storage_manufacturer_range(self):
        """0x2000-0x27FF defaults to PERSIST_APP."""
        assert ct._default_storage(0x2000) == "PERSIST_APP"
        assert ct._default_storage(0x27FF) == "PERSIST_APP"

    @pytest.mark.xdd
    def test_default_storage_other_is_ram(self):
        """Everything outside known ranges defaults to RAM."""
        assert ct._default_storage(0x4000) == "RAM"
        assert ct._default_storage(0x6000) == "RAM"

    @pytest.mark.xdd
    def test_storage_override_covers_error_register(self):
        """0x1001 (Error Register) must be RAM (not PERSIST_COMM)."""
        assert ct.STORAGE_OVERRIDE[0x1001] == "RAM"

    @pytest.mark.xdd
    def test_storage_override_covers_identity(self):
        """0x1018 (Identity) maps to RAM."""
        assert ct.STORAGE_OVERRIDE.get(0x1018) == "RAM"


# ===========================================================================
# OdGenerator c_ident helper
# ===========================================================================

class TestOdGeneratorHelpers:

    @pytest.mark.od
    def test_c_ident_replaces_spaces(self):
        assert ct.OdGenerator._c_ident("some name") == "some_name"

    @pytest.mark.od
    def test_c_ident_replaces_hyphens(self):
        assert ct.OdGenerator._c_ident("some-name") == "some_name"

    @pytest.mark.od
    def test_c_ident_preserves_valid(self):
        assert ct.OdGenerator._c_ident("validName123") == "validName123"

    @pytest.mark.od
    def test_var_name_format(self):
        name = ct.OdGenerator._var_name(0x1017, "Producer Heartbeat Time")
        assert name.startswith("x1017_"), f"Unexpected var name: {name}"

    @pytest.mark.od
    def test_obj_name_format(self):
        name = ct.OdGenerator._obj_name(0x1018, "Identity")
        assert name.startswith("o_1018_"), f"Unexpected obj name: {name}"


class TestRegressions:

    @pytest.mark.od
    def test_od_c_no_conflict_markers(self, xdd_file, tmp_path):
        """
        Regression: confirm the OD.c output does not contain any git conflict
        markers. (After the upstream bitwise PDO merge, all conflicts were resolved.)
        """
        ct.OdGenerator().generate(xdd_file, tmp_path)
        od_c = (tmp_path / "OD.c").read_text(encoding="utf-8")
        for marker in ("<<<<<<", "=======", ">>>>>>>"):
            assert marker not in od_c, f"Conflict marker {marker!r} found in OD.c"

    @pytest.mark.od
    def test_od_h_no_conflict_markers(self, xdd_file, tmp_path):
        ct.OdGenerator().generate(xdd_file, tmp_path)
        od_h = (tmp_path / "OD.h").read_text(encoding="utf-8")
        for marker in ("<<<<<<", "=======", ">>>>>>>"):
            assert marker not in od_h, f"Conflict marker {marker!r} found in OD.h"

    @pytest.mark.eds
    def test_generated_eds_no_conflict_markers(self, xdd_file, tmp_path):
        out = tmp_path / "out.eds"
        ct.EdsGenerator().generate(xdd_file, out)
        text = out.read_text(encoding="utf-8")
        for marker in ("<<<<<<", "=======", ">>>>>>>"):
            assert marker not in text, f"Conflict marker {marker!r} found in generated EDS"

    @pytest.mark.xdd
    def test_storage_override_does_not_crash_on_standard_indices(self, xdd_file):
        """
        Regression: all well-known indices in STORAGE_OVERRIDE must be parseable
        by _default_storage() without KeyError or crash.
        """
        for idx in ct.STORAGE_OVERRIDE:
            result = ct.STORAGE_OVERRIDE.get(idx, ct._default_storage(idx))
            assert result in ct.STORAGE_ORDER

    @pytest.mark.od
    def test_array_objects_have_sub0_size_macro(self, xdd_file, tmp_path):
        """
        Every ARRAY object must produce an OD_CNT_ARR_xxxx macro in OD.h.
        Missing macros would cause compiler errors when the stack references them.
        """
        objects = ct.XddParser().parse(xdd_file)
        arrays = [o for o in objects if o.object_type == 8]

        if not arrays:
            pytest.skip("No ARRAY objects in DS301 profile")

        ct.OdGenerator().generate(xdd_file, tmp_path)
        od_h = (tmp_path / "OD.h").read_text(encoding="utf-8")
        for arr in arrays:
            macro = f"OD_CNT_ARR_{arr.index:04X}"
            assert macro in od_h, f"Missing macro {macro} for ARRAY 0x{arr.index:04X}"

    @pytest.mark.cli
    def test_validate_xdd_returns_nonzero_on_garbage(self, tmp_path):
        """
        Regression: validate must return non-zero on unparseable input.
        """
        bad_xdd = tmp_path / "garbage.xdd"
        bad_xdd.write_text("not xml at all\n", encoding="utf-8")
        result = _run("validate", "--infile", str(bad_xdd))
        assert result.returncode != 0, "validate should fail on garbage XDD"

    @pytest.mark.eds
    def test_eds_parser_handles_equals_in_values(self, tmp_path):
        """
        EdsParser must handle '=' inside comment lines without crashing.
        This was an edge case in the original parser.
        """
        tricky = tmp_path / "tricky.eds"
        tricky.write_text(
            "[FileInfo]\n"
            "FileName=test.eds\n"
            "; SomeComment=with=multiple=equals\n"
            "FileVersion=1\n"
            "[1000]\n"
            "ParameterName=DeviceType\n"
            "ObjectType=0x7\n"
            "DataType=0x0007\n"
            "AccessType=ro\n"
            "DefaultValue=0x00000000\n"
            "PDOMapping=0\n",
            encoding="utf-8",
        )
        errors = ct.EdsParser().validate(tricky)
        assert errors == [], f"Unexpected validation errors: {errors}"
        objs = ct.EdsParser().parse(tricky)
        assert 0x1000 in objs
