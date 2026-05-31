# CANopenNode Test Suite

This directory contains all tests for the BitConcepts CANopenNode fork.
Three independent test layers exist; each can be run independently.

---

## 1. Upstream compile smoke test (from upstream)

The upstream CANopenNode ships a `Makefile` in `example/` that compiles the
full core stack against a blank CAN driver using `gcc`. This is the baseline
"does it build?" test inherited from upstream.

**Prerequisites:** GCC, Make (Linux/macOS/WSL)

```bash
make -C example clean all
```

Expected output: binary `example/canopennode_blank` with zero errors.
This test must remain green after every merge from upstream.

---

## 2. Python toolchain tests (pytest)

Tests for `tools/canopen_tools.py` — the iSMART OD/EDS toolchain.
Uses the reference `example/DS301_profile.xpd` and `example/DS301_profile.eds`
shipped with upstream as golden test inputs.

**Prerequisites:** Python 3.8+, pytest

```bash
pip install pytest
pytest tests/tools/ -v
```

### Coverage
| Class | What is tested |
|---|---|
| `TestXddParser` | XDD parsing: mandatory OD entries, types, storage groups, device info |
| `TestEdsParser` | EDS parsing: sections, sub-entries, validation, malformed input |
| `TestOdGenerator` | OD.c / OD.h output: include guards, struct types, sorted ODList |
| `TestEdsGenerator` | EDS output: roundtrip object count, CiA 306 structure |
| `TestTypeMaps` | XDD_TYPE_MAP, XDD_TO_EDS_DT, STORAGE_OVERRIDE correctness |
| `TestCLI` | All subcommands, compat/drop-in mode, help, error exit codes |
| `TestRegressions` | Conflict markers, STORAGE_OVERRIDE crash, ARRAY macro generation |

### Run subsets
```bash
pytest tests/tools/ -v -m xdd          # XDD parsing only
pytest tests/tools/ -v -m eds          # EDS parsing/generation only
pytest tests/tools/ -v -m od           # OD.c/OD.h generation only
pytest tests/tools/ -v -m cli          # CLI subcommands only
pytest tests/tools/ -v -k "regression" # regression tests only
```

---

## 3. Zephyr tests (west twister)

Zephyr-idiomatic tests covering the Zephyr integration module (`zephyr/`).

**Prerequisites:**
- A working Zephyr SDK and West workspace
- `native_sim` board support (included in every standard Zephyr install)
- The CANopenNode repo must be a West module (added to `west.yml`)

### 3a. Unit tests (ZTest on native_sim)

These tests run the Zephyr binary on the host and assert at runtime.

```bash
# Run all unit tests
west twister -T tests/zephyr/unit --platform native_sim -v

# Run config_bridge suite only
west twister -T tests/zephyr/unit/config_bridge --platform native_sim -v

# Build and run manually (no twister)
west build -b native_sim tests/zephyr/unit/config_bridge
./build/zephyr/zephyr.exe
```

#### config_bridge suite (`tests/zephyr/unit/config_bridge/`)
Verifies that `CO_zephyr_config.h` correctly maps Kconfig symbols to
`CO_CONFIG_*` bitmasks. Runs three testcase variants:

| Variant | What it tests |
|---|---|
| `canopen.config_bridge.base` | CO_CONFIG_PDO without BITWISE_MAPPING; NMT and storage bits |
| `canopen.config_bridge.pdo_bitwise` | CO_CONFIG_PDO_BITWISE_MAPPING bit is set (upstream PR #572) |
| `canopen.config_bridge.storage_ram` | BUG-003 regression: RAM backend symbol resolves correctly |

### 3b. Build-only tests (Kconfig matrix)

Pure compilation tests — no binary is executed. Each variant checks that
the module compiles cleanly with a different Kconfig combination. Build
failures indicate symbol mismatches, bad `#include` paths, or broken
`#ifdef` guards.

```bash
# Run all build tests
west twister -T tests/zephyr/build --platform native_sim -v

# Run just the storage regression guard
west twister -T tests/zephyr/build --platform native_sim \
    --tag storage -v
```

| Testcase | Key configs | What it guards |
|---|---|---|
| `canopen.build.minimal` | Core only, no PDO/storage/LEDs | Minimal linkage |
| `canopen.build.full` | PDO + storage + LEDs + SDO segmented | All features together |
| `canopen.build.pdo_bitwise` | `PDO_BITWISE_MAPPING=y` | Upstream PR #572 compile |
| `canopen.build.storage_settings` | `STORAGE_BACKEND_SETTINGS=y` | BUG-003 fix (correct prefix) |
| `canopen.build.lss` | `LSS_SLAVE=y` | LSS compile |
| `canopen.build.sdo_block` | `SDO_SERVER_BLOCK=y` | Block SDO + CRC16 |
| `canopen.build.no_rt_thread` | `RT_THREAD=n` | Manual process() mode |

### Run all Zephyr tests at once
```bash
west twister -T tests/zephyr --platform native_sim -v
```

---

## Run everything

```bash
# 1. Upstream compile smoke
make -C example clean all

# 2. Python toolchain
pytest tests/tools/ -v

# 3. Zephyr (requires West workspace)
west twister -T tests/zephyr --platform native_sim -v
```

---

## Bug regression index

The following bugs are directly tested by specific test cases.
See `doc/BUGFIXES.md` for full root-cause analysis of each bug.

| Bug | Test(s) that catch a regression |
|---|---|
| BUG-001 (prog_download `ret` vs `err`) | Manual code review + `canopen.build.full` |
| BUG-002 (RPDO pre-callback undefined symbol) | `canopen.config_bridge.base` (PDO callback bit) |
| BUG-003 (storage wrong Kconfig prefix) | `canopen.config_bridge.storage_ram`, `canopen.build.storage_settings` |
| BUG-004 (log level undefined) | `canopen.build.full` (compiler warning/error on undefined symbol) |
| BUG-005 (wrong weak hook name) | `TestRegressions` in Python tests (API shape) |
| Upstream PR #572 (bitwise PDO) | `canopen.config_bridge.pdo_bitwise`, `canopen.build.pdo_bitwise`, `TestRegressions.test_od_c_no_conflict_markers` |
