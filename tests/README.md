# CANopenNode Test Suite

This directory contains all tests for the BitConcepts CANopenNode fork.
Three independent test layers exist; each can be run independently.

## Quick start

```bash
# Layer 1: upstream compile smoke (requires GCC + Make on Linux/WSL)
make -C example clean all

# Layer 2: Python toolchain (requires Python 3.8+ and pytest)
pip install pytest
pytest tests/tools/ -v

# Layer 3a: Zephyr ZTest — native_sim (fastest, no QEMU needed)
west twister -T tests/zephyr --platform native_sim -v

# Layer 3b: QEMU simulation (full ISR/timer accuracy)
west twister -T tests/zephyr --platform qemu_x86 -v
west twister -T tests/zephyr --platform qemu_cortex_m3 -v

# Layer 3c: All platforms at once
west twister -T tests/zephyr --platform native_sim --platform qemu_x86 --platform qemu_cortex_m3 -v
```

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

### What native_sim vs QEMU mean

| Platform | Execution | CAN | Timer accuracy | When to use |
|---|---|---|---|---|
| `native_sim` | Host Linux process | can_loopback | Host clock | Fast CI, development |
| `qemu_x86` | QEMU VM | can_loopback | QEMU 8253 PIT | ISR timing, scheduler accuracy |
| `qemu_cortex_m3` | QEMU ARM VM | can_loopback | SysTick | ARM endianness/ISR validation |

All three use Zephyr's **`can_loopback`** driver: TX frames are immediately
reflected as RX on the same device, giving real end-to-end CANopen protocol
exchange without any physical CAN hardware.

**Prerequisites:**
- Zephyr SDK ≥ 3.6, West workspace, `ZEPHYR_BASE` set
- QEMU installed (for `qemu_x86` / `qemu_cortex_m3` targets)
- CANopenNode added as a West module in your app's `west.yml`

### 3a. Unit tests (ZTest runtime assertions)

```bash
# All unit suites, native_sim
west twister -T tests/zephyr/unit --platform native_sim -v

# All unit suites, QEMU x86
west twister -T tests/zephyr/unit --platform qemu_x86 -v

# All unit suites, ARM Cortex-M3
west twister -T tests/zephyr/unit --platform qemu_cortex_m3 -v

# Build and run a single suite manually (no twister)
west build -b native_sim tests/zephyr/unit/integration
./build/zephyr/zephyr.exe    # native_sim
west build -b qemu_x86 tests/zephyr/unit/integration && west build -t run
```

#### `integration/` — API lifecycle (28 test cases)
Tests `canopen_start()`, `canopen_stop()`, `canopen_is_running()`,
arg validation, double-start, error helpers, CO pointer safety.

| Testcase | Platform |
|---|---|
| `canopen.integration.native_sim` | native_sim |
| `canopen.integration.qemu_x86` | qemu_x86 |
| `canopen.integration.qemu_cortex_m3` | qemu_cortex_m3 |

#### `protocol/` — CANopen protocol frames (11 test cases)
Real CANopen frame exchange via can_loopback: bootup frame (CiA 301 §7.5.2.2),
heartbeat producer (§7.5.2.3), NMT commands (stop/pre-op/start/broadcast),
SDO expedited upload/abort (§7.2.4.3).

| Testcase | Platform |
|---|---|
| `canopen.protocol.native_sim` | native_sim |
| `canopen.protocol.qemu_x86` | qemu_x86 |
| `canopen.protocol.qemu_cortex_m3` | qemu_cortex_m3 |

#### `od_interface/` — Object Dictionary access (18 test cases)
Pure OD data-access functions, no CAN required: `OD_find()`, `OD_getSub()`,
`OD_get_u8/u16/u32()`, `OD_set_u16()`, read-only rejection,
invalid index/sub-index handling.

| Testcase | Platform |
|---|---|
| `canopen.od_interface.native_sim` | native_sim |
| `canopen.od_interface.qemu_x86` | qemu_x86 |
| `canopen.od_interface.qemu_cortex_m3` | qemu_cortex_m3 |

#### `config_bridge/` — Kconfig→CO_CONFIG_* bitmask (5 variants)
Verifies `CO_zephyr_config.h` ZBIT() composition; includes storage BUG-003
regression and PR #572 bitwise PDO bit.

#### `pdo_bitwise/` — PDO bitwise mapping runtime (6 test cases)
Runtime flag checks and CO_PDO_size_t semantics for the bitwise mapping
feature from upstream PR #572.

### 3b. Build-only tests (Kconfig matrix)

```bash
west twister -T tests/zephyr/build --platform native_sim -v
west twister -T tests/zephyr/build --platform qemu_x86 --tag storage -v  # regression
```

| Testcase | Key configs | What it guards |
|---|---|---|
| `canopen.build.minimal` | Core only | Minimal linkage |
| `canopen.build.full` | PDO + storage + LEDs + SDO | All features together |
| `canopen.build.pdo_bitwise` | `PDO_BITWISE_MAPPING=y` | Upstream PR #572 compile |
| `canopen.build.storage_settings` | `STORAGE_BACKEND_SETTINGS=y` | BUG-003 fix |
| `canopen.build.lss` | `LSS_SLAVE=y` | LSS compile |
| `canopen.build.sdo_block` | `SDO_SERVER_BLOCK=y` | Block SDO + CRC16 |
| `canopen.build.no_rt_thread` | `RT_THREAD=n` | Manual process() mode |

### Run all Zephyr tests at once
```bash
# native_sim (fast CI)
west twister -T tests/zephyr --platform native_sim -v

# Full QEMU simulation sweep
west twister -T tests/zephyr \
    --platform native_sim \
    --platform qemu_x86 \
    --platform qemu_cortex_m3 -v

# By tag
west twister -T tests/zephyr --platform native_sim --tag regression -v
west twister -T tests/zephyr --platform qemu_x86   --tag qemu -v
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
| BUG-001 (prog_download `ret` vs `err`) | `canopen.integration.*` (start/stop flow), `canopen.build.full` |
| BUG-002 (RPDO pre-callback undefined symbol) | `canopen.protocol.*` (RPDO pre-signal latency), `canopen.config_bridge.base` |
| BUG-003 (storage wrong Kconfig prefix) | `canopen.config_bridge.storage_ram`, `canopen.build.storage_settings` |
| BUG-004 (log level undefined) | `canopen.build.full` (compile-time symbol check) |
| BUG-005 (wrong weak hook name) | `canopen.integration.*/test_default_node_id_from_kconfig` |
| Upstream PR #572 (bitwise PDO) | `canopen.pdo_bitwise.*`, `canopen.config_bridge.pdo_bitwise`, `canopen.build.pdo_bitwise` |

---

## Full test inventory (all ~95 test cases)

| Suite | Layer | Cases | Platforms | Key coverage |
|---|---|---|---|---|
| `example/Makefile` | 1 | 1 (compile) | Linux/WSL | Upstream core stack |
| `TestXddParser` | 2 | 13 | Python | XDD parse, device info |
| `TestEdsParser` | 2 | 7 | Python | EDS parse, validation |
| `TestOdGenerator` | 2 | 14 | Python | OD.h/OD.c structure, sorted list |
| `TestEdsGenerator` | 2 | 7 | Python | EDS roundtrip, CiA 306 |
| `TestTypeMaps` | 2 | 4 | Python | Type map completeness |
| `TestCLI` | 2 | 8 | Python | All subcommands, compat mode |
| `TestEdsDescPatcher` | 2 | 3 | Python | patch-desc idempotency |
| `TestAccessTypeMapping` | 2 | 7 | Python | ro/rw/wo mapping, $NODEID |
| `TestStorageGroupLogic` | 2 | 5 | Python | Storage group ranges |
| `TestOdGeneratorHelpers` | 2 | 5 | Python | c_ident, var_name helpers |
| `TestRegressions` | 2 | 6 | Python | Conflict markers, BUG fixes |
| `canopen.integration.*` | 3a | 28 ×3 | native_sim, qemu_x86, qemu_cortex_m3 | API lifecycle, arg validation |
| `canopen.protocol.*` | 3a | 11 ×3 | native_sim, qemu_x86, qemu_cortex_m3 | Bootup, HB, NMT cmd, SDO |
| `canopen.od_interface.*` | 3a | 18 ×3 | native_sim, qemu_x86, qemu_cortex_m3 | OD get/set/find |
| `canopen.config_bridge.*` | 3a | 5 | native_sim, qemu_x86 | ZBIT bitmask correctness |
| `canopen.pdo_bitwise.*` | 3a | 6 ×3 | native_sim, qemu_x86, qemu_cortex_m3 | PR #572 runtime |
| `canopen.build.*` | 3b | 7 | native_sim | Kconfig compile matrix |
