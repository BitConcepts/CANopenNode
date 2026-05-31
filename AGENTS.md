# AGENTS.md — CANopenNode (iSMART / BitConcepts fork)

This file provides context and ground rules for AI coding agents working in this repository.

---

## Project overview

This is **CANopenNode v4** — a MISRA C:2012-compliant, ANSI C CANopen protocol stack — extended with:

- A first-class **Zephyr RTOS module** (`zephyr/`) contributed by BitConcepts.
- A unified Python toolchain (`tools/canopen_tools.py`) for Object Dictionary code generation, EDS conversion, and firmware config-layer artifact generation.

Target hardware: primarily the **Variscite DART-MX8M-Plus** (VAR-DT8MCustomBoard) running Zephyr, but the core stack is hardware-agnostic.

---

## Critical rule: OD.c / OD.h are auto-generated — never edit them manually

`OD.c` and `OD.h` (wherever they appear in consumer applications) are **generated output**. The authoritative source is the **XDD file** (CANopenEditor XML format). Modifying `OD.c` or `OD.h` by hand will be overwritten on the next generation pass.

The generation pipeline is:

```
ismart-control-board.xdd   →  canopen_tools.py xdd2od   →  OD.h / OD.c
ismart-control-board.xdd   →  canopen_tools.py xdd2eds  →  ismart-control-board.eds
ismart-control-board.eds   →  canopen_tools.py eds2config →  config_od_bindings.inc
                                                             config_data_schema.h
                                                             config_data_schema.inc
```

---

## Repository layout

```
CANopenNode/
├── 301/          Core CANopen objects (NMT, HB, PDO, SDO, SYNC, TIME, …)
├── 302/          Program Download / firmware update (CiA 302-3)
├── 303/          CiA 303-3 LED indicators
├── 304/          CiA 304 CANopen Safety (SRDO, GFC)
├── 305/          CiA 305 LSS (Layer Setting Services)
├── 309/          CiA 309-3 ASCII gateway (NMT/LSS/SDO over TCP)
├── extra/        CO_trace — variable recording over time
├── storage/      OD data storage (base + eeprom backend)
├── example/      Blank/template driver and main for new ports
├── doc/          Doxygen inputs, CHANGELOG, device support list
├── tools/
│   └── canopen_tools.py   iSMART unified OD/EDS/config toolchain (see below)
├── zephyr/                Zephyr module integration (see below)
│   ├── Kconfig            Full Kconfig tree for every CANopenNode feature
│   ├── CMakeLists.txt     Module CMake; drives OD generation from XDD/EDS
│   ├── requirements.txt   Python deps note (eds-utils optional)
│   ├── include/           Public Zephyr integration headers
│   └── *.c                Zephyr backend: driver, integration, leds, storage
├── CANopen.h / CANopen.c  High-level init/processing for common configurations
├── .clang-format          Code-formatting rules — always run clang-format
└── MISRA.md               PC Lint Plus inhibit list and MISRA exception notes
```

---

## tools/canopen_tools.py

Single-file Python 3 CLI. No external dependencies for XDD input; `eds-utils` is required only when consuming an EDS directly.

| Subcommand   | Purpose |
|---|---|
| `xdd2od`     | XDD → `OD.h` + `OD.c` (CANopenNode V4 format) |
| `xdd2eds`    | XDD → EDS (CiA 306) |
| `eds2config` | EDS manufacturer block (0x2200–0x22FF) → three C config artifacts |
| `validate`   | Validate an EDS or XDD for parse errors |
| `patch-desc` | Insert `;Description=` annotations into an EDS in-place |

**Auto-discovery:** without `--xdd`/`--eds` flags the tool walks up from its own location to find `application/canopen/ismart-control-board.{xdd,eds}`.

**Compat mode:** if the first argument is a file path (not a subcommand) the tool acts as a drop-in for the old `eds2c_wrapper.py` — useful for CMake integration.

```pwsh
# Generate OD files from XDD
python tools/canopen_tools.py xdd2od --xdd path/to/device.xdd --outdir build/od

# Generate EDS from XDD
python tools/canopen_tools.py xdd2eds --xdd path/to/device.xdd --out build/device.eds

# Generate firmware config artifacts (reads EDS, writes into application/ tree)
python tools/canopen_tools.py eds2config --eds path/to/device.eds --repo /path/to/repo/root

# Check whether config artifacts are stale (CI-friendly, no writes)
python tools/canopen_tools.py eds2config --check
```

Storage group mapping (relevant when reading generated code):

| Storage group   | C struct / attribute macro |
|---|---|
| `PERSIST_COMM`  | `OD_PERSIST_COMM_t` / `OD_ATTR_PERSIST_COMM` |
| `RAM`           | `OD_RAM_t` / `OD_ATTR_RAM` |
| `PERSIST_APP`   | `OD_PERSIST_APP_t` / `OD_ATTR_PERSIST_APP` |
| `ROM`           | `OD_ROM_t` / `OD_ATTR_ROM` |

---

## zephyr/ module

The Zephyr module lives entirely in `zephyr/` and is consumed via West manifest.

### Key source files

| File | Role |
|---|---|
| `CO_zephyr_driver.c` | CAN hardware backend; adapts Zephyr `drivers/can` to CANopenNode `CO_driver.h` |
| `CO_zephyr_integration.c/.h` | Runtime start/stop API (`canopen_start()` / `canopen_stop()`) |
| `CO_zephyr_leds.c/.h` | CiA 303-3 LED bridge — mirrors RUN/ERR to DT aliases `co-led-run` / `co-led-err` |
| `CO_zephyr_storage.c/.h` | Plugs CANopenNode storage into Zephyr `settings` subsystem or RAM-only mode |
| `CO_zephyr_prog_download.c/.h` | Optional firmware-download helper |
| `include/CO_driver_target.h` | Zephyr-specific type definitions, locking primitives, and ISR helpers |
| `Kconfig` | Exposes every CANopenNode feature as `CONFIG_CANOPENNODE_*` symbols |
| `CMakeLists.txt` | Builds the module; optionally runs `canopen_tools.py` to generate OD from XDD/EDS |

### Kconfig conventions

- Top-level gate: `CONFIG_CANOPENNODE=y` requires `CONFIG_CAN=y`.
- Protocol modules are individually switchable (NMT, HB consumer, EM, SDO server, SDO client, SYNC, PDO, TIME, LSS, gateway, LEDs, storage).
- `select` is used to enforce required dependencies (e.g., block SDO selects segmented SDO and CRC16).
- Feature flags that compile to `CO_config.h` macros follow the pattern `CANOPENNODE_<FEATURE>_<OPTION>`.

### OD generation via CMake

Set `CONFIG_CANOPENNODE_EDS_FILE_PATH` to an XDD or EDS path relative to your application root. CMake calls `canopen_tools.py` automatically and exports `CO_OD_H`, `CO_OD_C`, and `CO_OD_DIR`.

XDD input is preferred — it requires no extra Python packages. EDS input requires `pip install eds-utils`.

---

## Coding standards

### C code (stack core and Zephyr backend)

- **Standard:** ANSI C (C99 compatible). Object-oriented style using structs and function pointers.
- **MISRA C:2012:** The core stack files comply; see `MISRA.md` for the PC Lint Plus inhibit list and documented exceptions. `OD.c`/`OD.h` are excluded from MISRA checking by design.
- **Formatting:** `.clang-format` is present at the repo root. Run `clang-format -i` on any C/H file you modify.
- **No dynamic allocation:** Do not use `malloc`/`free`. Define `CO_USE_GLOBALS` in driver config for global allocation. See `MISRA.md`.
- **Non-blocking:** All CANopenNode stack code must remain non-blocking. Never block or busy-wait inside protocol processing functions.
- **Include guard convention:** `#ifndef FOO_H` / `#define FOO_H` / … / `#endif /* FOO_H */`

### Python toolchain (`tools/canopen_tools.py`)

- Single-file, stdlib-only (no third-party imports except optional `eds-utils` at CLI entry).
- Python 3.8+.
- Use dataclasses and type annotations throughout.
- Preserve the existing class structure: `XddParser`, `EdsParser`, `OdGenerator`, `EdsGenerator`, `ConfigGenerator`, `EdsDescPatcher`.

---

## Object Dictionary conventions

- **Index ranges:**
  - `0x1000–0x1FFF`: Standard communication objects (CiA 301). Storage default: `PERSIST_COMM`.
  - `0x2000–0x27FF`: Manufacturer-specific objects. Storage default: `PERSIST_APP`.
  - `0x2200–0x22FF`: iSMART firmware configuration block (consumed by `eds2config`).
- **Storage override table** in `canopen_tools.py` (`STORAGE_OVERRIDE`) takes precedence over the range-based default for specific well-known indices.
- `ODList[]` **must be sorted ascending by index** — `OD_find()` uses binary search. The generators enforce this; do not reorder entries manually.

---

## CAN interface (iSMART platform)

- Default interface: `can0` (SocketCAN / gc-usb USB-CAN adapter or on-SoC CAN).
- Bitrate is configured at runtime via systemd unit or `canopen_start()` parameter.
- Default bitrate for iSMART: **500 kbit/s**.
- CAN interface management is available to users in the `can` group.

---

## What NOT to do

- **Do not edit `OD.c` or `OD.h` by hand.** Re-run `canopen_tools.py xdd2od` instead.
- **Do not edit the EDS directly as a primary source.** The XDD is the source of truth; the EDS is derived output.
- **Do not add audio/ALSA dependencies.** This platform has no audio support.
- **Do not add blocking calls** inside CANopenNode processing functions (timer thread, receive thread).
- **Do not use dynamic memory allocation** (`malloc`, `calloc`, `realloc`, `free`) in C stack code.
- **Do not commit generated files** (`OD.c`, `OD.h`, `*.eds` if derived, config artifacts) unless the project's build system requires them to be committed.

---

## Testing and validation

- **EDS/XDD validation:** `python tools/canopen_tools.py validate --infile path/to/file.xdd`
- **Doxygen:** `doxygen` from the repo root generates HTML docs into `doc/html/`. Requires `doxygen` and `graphviz`.
- **Zephyr:** Build and test via West (`west build`, `west flash`, `west twister`). There is no standalone unit-test runner for the core stack in this repo; see [CANopenDemo](https://github.com/CANopenNode/CANopenDemo) for integration tests.
- **Config artifact staleness check (CI):** `python tools/canopen_tools.py eds2config --check` exits 1 if outputs would change.

---

## Key external references

- CANopenNode upstream: https://github.com/CANopenNode/CANopenNode
- CANopenEditor (OD GUI): https://github.com/CANopenNode/CANopenEditor — use exporter preset `CANopenNode_V4`
- Generated HTML docs: https://canopennode.github.io
- CiA 301 (CANopen application layer): https://www.can-cia.org
