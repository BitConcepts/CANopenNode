# Bug Fix Registry — BitConcepts CANopenNode Fork

This document tracks every bug found and fixed in the BitConcepts fork of CANopenNode.
Each entry records the root cause, impact, fix, and upstream relevance so that fixes
which belong in the upstream project can be submitted as PRs with full context.

Format is kept compatible with the upstream `doc/CHANGELOG.md` style.

---

## BUG-001 — Program Download bind error silently ignored

**Severity:** High  
**Discovered:** 2026-05-31 during code audit  
**Fixed in commit:** `b798758`  
**Upstream relevance:** BitConcepts fork only (`CO_zephyr_integration.c` is not in upstream).  
No upstream PR needed.

### Affected file
`zephyr/CO_zephyr_integration.c` — `canopen_start()` function, `CONFIG_CANOPENNODE_PROG_DOWNLOAD` block.

### Root cause
The error check after `CO_Prog_Download_zephyr_bind_default()` compared the pre-existing
variable `ret` (which is always `0` / `CO_ERROR_NO` at that point in the function) instead
of `err` (the return value of the bind call itself).

```c
// WRONG — before fix
err = CO_Prog_Download_zephyr_bind_default(&pdl, &zb_ctx);
if (ret != CO_ERROR_NO) {          // ret is always 0 here — check is dead
    LOG_ERR("Program Download bind failed: %d", ret);  // also logs wrong value

// CORRECT — after fix
err = CO_Prog_Download_zephyr_bind_default(&pdl, &zb_ctx);
if (err != CO_ERROR_NO) {          // checks the actual return value
    LOG_ERR("Program Download bind failed: %d", err);
```

### Impact
If `CO_Prog_Download_zephyr_bind_default()` failed (e.g., because the flash area was not
configured correctly or the internal ops pointer registration failed), `canopen_start()`
would return `0` (success) anyway. Any subsequent CANopen program-download request from
the network would reach uninitialised function pointers in the ops struct, producing
undefined behaviour (likely a crash or silent data corruption).

### How to reproduce
1. Configure a board where `FLASH_AREA_IMAGE_SECONDARY` is not available or returns an error.
2. Enable `CONFIG_CANOPENNODE_PROG_DOWNLOAD=y`.
3. Observe that `canopen_start()` returns 0 despite the bind failure.

### Fix
Changed `ret` → `err` on both the condition and the log argument.

---

## BUG-002 — RPDO pre-callback never registered (undefined Kconfig symbol)

**Severity:** Medium  
**Discovered:** 2026-05-31 during code audit  
**Fixed in commit:** `b798758`  
**Upstream relevance:** BitConcepts fork only. No upstream PR needed.

### Affected file
`zephyr/CO_zephyr_integration.c` — `z_enable_pre_signals()` function.

### Root cause
The preprocessor guard for registering the RPDO pre-callback used a Kconfig symbol that
does not exist:

```c
// WRONG — symbol CONFIG_CANOPENNODE_RPDO_CALLBACK is not defined in Kconfig
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE) && IS_ENABLED(CONFIG_CANOPENNODE_RPDO_CALLBACK)

// CORRECT — the actual symbol that covers both RPDO and TPDO callbacks
#if IS_ENABLED(CONFIG_CANOPENNODE_RPDO_ENABLE) && IS_ENABLED(CONFIG_CANOPENNODE_PDO_CALLBACK)
```

`IS_ENABLED()` on an undefined symbol always evaluates to 0 (false) in Zephyr, so the
entire `CO_RPDO_initCallbackPre()` loop was compiled out unconditionally.

### Impact
The RT thread's semaphore (`rt_sem`) was never given on RPDO reception. The thread
continued to run on its periodic fallback timeout (default 1 ms) but missed the
prompt wake-up that reduces RPDO → application latency. Under high-load conditions
where the 1 ms fallback was not fast enough, RPDO data could be delayed by up to
one fallback period.

Additionally, because the intent of the pre-callback is to avoid missing RPDOs during
rapid reception bursts, this bug could cause RPDO frames to be overwritten in the
double-buffer before the thread processed them.

### Fix
Changed `CONFIG_CANOPENNODE_RPDO_CALLBACK` → `CONFIG_CANOPENNODE_PDO_CALLBACK`
(the existing Kconfig symbol that is `y` by default when PDO is enabled).

---

## BUG-003 — OD storage backend selection completely broken (wrong Kconfig prefix)

**Severity:** Critical  
**Discovered:** 2026-05-31 during code audit  
**Fixed in commit:** `b798758`  
**Upstream relevance:** BitConcepts fork only. No upstream PR needed.

### Affected file
`zephyr/CO_zephyr_storage.c` — six `#ifdef` / `#if defined()` guards throughout the file.

### Root cause
All six backend-selection guards used the prefix `CONFIG_CANOPEN_STORAGE_BACKEND_*` instead
of `CONFIG_CANOPENNODE_STORAGE_BACKEND_*` (note the missing `NODE` segment):

```c
// WRONG — prefix is CONFIG_CANOPEN_ (never defined)
#ifdef CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS
#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)

// CORRECT — matches Kconfig symbols in zephyr/Kconfig
#ifdef CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS
#if defined(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
```

The Kconfig tree defines `CANOPENNODE_STORAGE_BACKEND_SETTINGS` and
`CANOPENNODE_STORAGE_BACKEND_RAM`. Undefined macros evaluate to 0 in C preprocessor
conditionals, so every backend path was dead code.

### Affected locations (all in `CO_zephyr_storage.c`)
| Line (pre-fix) | Guard | Purpose |
|---|---|---|
| 38 | `#ifdef CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS` | `#include <settings.h>` |
| 48 | `#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS)` | `z_store()` Settings path |
| 56 | `#elif defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)` | `z_store()` RAM path |
| 70 | `#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS)` | `z_restore()` Settings path |
| 77 | `#elif defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)` | `z_restore()` RAM path |
| 118 | `#if defined(CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS)` | boot-time load |
| 125 | `#elif defined(CONFIG_CANOPEN_STORAGE_BACKEND_RAM)` | boot-time RAM skip |

### Impact
All OD persistence operations silently became no-ops:
- Store (OD 0x1010): Parameters were never written to flash/NVS.
- Restore (OD 0x1011): Persisted keys were never deleted.
- Boot load: Persisted values were never loaded back into RAM at startup.

The only side-effect visible at runtime was a LOG_WRN message: *"No valid storage backend
selected — store operation skipped"* — easy to overlook in production logs.

### Root cause analysis
The `.c` file was originally written against a draft Kconfig that used the shorter
`CANOPEN_` prefix. The Kconfig was later standardised to `CANOPENNODE_` to match all
other module symbols, but the `.c` was not updated at the same time.

### Fix
Changed all six occurrences to use the correct `CONFIG_CANOPENNODE_STORAGE_BACKEND_*` prefix.

---

## BUG-004 — `CONFIG_CANOPEN_LOG_LEVEL` used but never declared in Kconfig

**Severity:** Low (functional, but logs silenced by default)  
**Discovered:** 2026-05-31 during code audit  
**Fixed in commit:** `b798758`  
**Upstream relevance:** BitConcepts fork only. No upstream PR needed.

### Affected files
- `zephyr/CO_zephyr_integration.c` line 48
- `zephyr/CO_zephyr_driver.c` line 39
- `zephyr/CO_zephyr_storage.c` line 36
- `zephyr/CO_zephyr_prog_download.c` line 12

All use: `LOG_MODULE_REGISTER(<name>, CONFIG_CANOPEN_LOG_LEVEL)`

### Root cause
`CONFIG_CANOPEN_LOG_LEVEL` was referenced in source but never declared via the standard
Zephyr logging Kconfig template. In Zephyr's logging subsystem, an undefined log-level
symbol evaluates to `0` (`LOG_LEVEL_NONE`), silencing all log output from these modules.

### Impact
All `LOG_ERR`, `LOG_WRN`, `LOG_INF`, and `LOG_DBG` calls in the four files produced no
output when `CONFIG_LOG=y`, making debugging the integration layer significantly harder.

### Fix
Added a `Logging` menu at the bottom of `zephyr/Kconfig` using the standard Zephyr
template:

```kconfig
menu "Logging"
module = CANOPEN
module-str = CANopen
source "$(ZEPHYR_BASE)/subsys/logging/Kconfig.template.log_config"
endmenu
```

This generates `CONFIG_CANOPEN_LOG_LEVEL` as a user-configurable integer with the
standard `LOG_LEVEL_*` choices, consistent with all other Zephyr modules.

---

## BUG-005 — `canopen_get_node_id_hook` declared in header but never implemented or called

**Severity:** Low (documentation/API mislead)  
**Discovered:** 2026-05-31 during code audit  
**Fixed in commit:** `b798758`  
**Upstream relevance:** BitConcepts fork only. No upstream PR needed.

### Affected file
`zephyr/include/CO_zephyr_integration.h` — near end of file.

### Root cause
The public header declared a weak hook:
```c
__weak uint8_t canopen_get_node_id_hook(void *ud);
```
But the implementation (`CO_zephyr_integration.c`) defined and called a different function:
```c
__weak uint8_t canopen_get_node_id(void);  // no _hook suffix, no argument
```

The two symbols are entirely separate. An application developer reading the header would
implement `canopen_get_node_id_hook(void *ud)`, which would never be called. Their custom
Node-ID logic would be silently ignored and the Kconfig default would always be used.

### Impact
Applications that followed the header documentation to supply a dynamic Node-ID (e.g.,
from DIP switches or NVS) would see their override ignored. The stack would always use
`CONFIG_CANOPENNODE_INIT_NODE_ID`.

### Fix
Removed the stale `canopen_get_node_id_hook` declaration and replaced it with the
declaration of the actual weak function `canopen_get_node_id(void)` that the integration
uses, with correct documentation.

---

## BUG-006 — `RPDO_CALLBACK` Kconfig symbol in `CO_zephyr_integration.c` references undefined symbol

*(Alias for BUG-002 — same commit, same fix. Listed separately for search indexing.)*

---

## BUG-007 — README and AGENTS.md used old API names `co_canopen_start/stop()`

**Severity:** Low (documentation only)  
**Discovered:** 2026-05-31 during code audit  
**Fixed in commit:** `b798758`  
**Upstream relevance:** Not applicable (documentation files are fork-specific).

### Details
The public API in `zephyr/include/CO_zephyr_integration.h` exposes `canopen_start()` and
`canopen_stop()`. The README.md Quick Start section and AGENTS.md both referenced the old
names `co_canopen_start()` / `co_canopen_stop()` from a previous iteration of the API.

Additionally, `README.md` step 6 (storage) used `CONFIG_CANOPEN_STORAGE_BACKEND_SETTINGS`
(wrong prefix — see BUG-003) instead of `CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS`.

All three occurrences corrected.

---

## How to search for these fixes in git

```bash
# Find the fix commit
git log --all --grep "BUG-0" --oneline

# See everything changed in the audit fix commit
git show b798758 --stat

# Diff a specific file across the fix
git diff b798758^..b798758 -- zephyr/CO_zephyr_storage.c
```

---

## Upstream PR candidates

As of 2026-05-31, none of the bugs above are in upstream
[CANopenNode/CANopenNode](https://github.com/CANopenNode/CANopenNode). All bugs are
confined to the BitConcepts Zephyr integration layer (`zephyr/`) which is not present
in upstream. No upstream PRs are required.

If any of these files are contributed upstream in the future, the fix commits and this
document should be referenced in the PR description.

---

*Document maintained by BitConcepts, LLC. See also `doc/CHANGELOG.md`.*
