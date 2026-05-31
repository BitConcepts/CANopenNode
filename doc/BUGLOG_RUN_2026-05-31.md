# Bug Log — Full Test Run 2026-05-31

Tests run:
- `python -m pytest tests/tools/ -v` — Windows, Python 3.11.9, pytest 9.0.3
- `make -C example clean all` — WSL Ubuntu-24.04, GCC

---

## Test Results Summary

| Test layer | Command | Result |
|---|---|---|
| Upstream smoke | `make -C example clean all` | **FAILED** before fix → **PASS** after |
| Python toolchain | `pytest tests/tools/` | **2 FAILED**, 110 warnings before → **117/117 PASS, 0 warnings** after |
| Zephyr ZTest | `west twister` (requires Zephyr SDK) | Not run — west not in WSL PATH |

---

## Bugs Found and Fixed (commit 3b951d2)

### BUG-A — `303/CO_LEDs.c:198` — Compile blocker: `#endif }` syntax error
**Severity:** Critical (blocks `make -C example`)
**Found by:** `make -C example clean all` in WSL
**Error:** `error: expected declaration or statement at end of input`
**Root cause:** The closing brace `}` of `CO_LEDs_process()` was fused onto the
  `#endif` directive on the same line. GCC treats `#endif` as a preprocessor
  directive and ignores the trailing `}`, leaving the function body never closed.
**Fix:** Split `#endif }` into `#endif\n} /* CO_LEDs_process */`
**Upstream relevance:** BitConcepts added the LEDS_CALLBACK feature to CO_LEDs.c;
  upstream does not have this extension. No upstream PR needed.

---

### BUG-B/C/D — `303/CO_LEDs.c` — Three wrong `CO_CONFIG_LEDS_CALLBACK` guards
**Severity:** High (wrong feature gating, always-on regardless of config)
**Found by:** Code review during CO_LEDs.c fix (related to BUG-A)
**Root cause:** Three `#if` guards used the constant's value directly:
```c
// WRONG — always true because CO_CONFIG_LEDS_CALLBACK == 0x02
#if (CO_CONFIG_LEDS_CALLBACK) != 0   // lines 37 and 46
#if CO_CONFIG_LEDS_CALLBACK           // line 159
```
`CO_CONFIG_LEDS_CALLBACK` is a bitmask flag (= `0x02`) intended to be OR'd into
`CO_CONFIG_LEDS`. Checking the constant directly is always true, so callback
fields and `CO_LEDs_registerCallback()` were compiled in unconditionally even
when the feature was not configured.
```c
// CORRECT — checks whether the bit is enabled in the feature aggregate
#if ((CO_CONFIG_LEDS) & CO_CONFIG_LEDS_CALLBACK) != 0
```
**Fix:** All three occurrences corrected to the proper bitmask form.
**Upstream relevance:** BitConcepts-only extension. No upstream PR needed.

---

### BUG-E — `tools/canopen_tools.py:_compat_mode` — `.xpd` not recognised as XDD
**Severity:** Medium (compat mode unusable with CANopenEditor v1.0 files)
**Found by:** `pytest` — `TestCLI::test_compat_mode_xdd_input` FAILED
**Error:** `ERROR: cannot find XDD for .../DS301_profile.xpd`
**Root cause:** `_compat_mode()` only recognised `.xdd` (CiA 311 v1.1) as a valid
  XDD extension. The reference file `example/DS301_profile.xpd` uses the
  `.xpd` extension (CANopenEditor v1.0 format — functionally identical XML).
  When passed a `.xpd` file, the function looked for `DS301_profile.xdd`
  (doesn't exist) then `ismart-control-board.xdd` (doesn't exist) and failed.
**Fix:** Treat both `.xdd` and `.xpd` as XDD formats. If the input IS a `.xpd`
  file, use it directly. If the input is an EDS, try sibling `.xdd` then `.xpd`.

---

### BUG-F — `tools/canopen_tools.py:cmd_validate` — `.xpd` falls through to EDS parser
**Severity:** Medium (validate gives wrong result for .xpd files)
**Found by:** Code review during BUG-E fix
**Root cause:** `cmd_validate()` only routed `.xdd` to `XddParser`; `.xpd` fell
  through to the EDS parser path, which would either error or produce wrong output.
**Fix:** Add `.xpd` to the XDD suffix check alongside `.xdd`.

---

### BUG-G — `tests/tools/pyproject.toml` — Pytest markers in wrong TOML form
**Severity:** Low (110+ warnings per run, `-m xdd` etc. ignored)
**Found by:** `pytest` — 110 `PytestUnknownMarkWarning` per run
**Root cause:** Markers defined as a nested TOML section:
```toml
[tool.pytest.ini_options.markers]   # WRONG — pytest ignores this
eds = "..."
```
pytest expects markers as a list under the main section:
```toml
[tool.pytest.ini_options]
markers = ["eds: ...", "xdd: ..."]  # CORRECT
```
**Fix:** Converted to the correct list form.

---

## Upstream CANopenNode Issues Comparison

Upstream open issues reviewed at: https://github.com/CANopenNode/CANopenNode/issues

| Issue | Title | Overlap with our fork? |
|---|---|---|
| #454 | SDO read/write notification callback | Feature request; our SDO tests pass, not affected |
| #536 | CO_trace.h uses old `CO_SDO_t` type | CO_trace disabled in our Kconfig; not triggered |
| #557 | OD_CNT_NMT error with NMTStartup | User config error; our OD generator avoids this |
| #565 | NMT Master HardFault on init | User init order error; unrelated to our fork |
| #566 | Error History logs clearing of errors | Behaviour question; our EMCY test passes |
| #581 | Refactor to Rust | Out of scope |
| #584 | SDO rejects DLC < 8 | By-design per CiA 301; our SDO test uses DLC=8 |
| #599 | Broken deviceSupport.md link | Doc only; no code impact |
| #607 | CAN FD support request | Future feature; out of scope |

**Conclusion:** None of the open upstream issues overlap with the bugs found in
our fork. All bugs found (BUG-A through BUG-G) are confined to BitConcepts fork
files (`303/CO_LEDs.c` with our callback extension, `tools/canopen_tools.py`,
`tests/tools/pyproject.toml`). No upstream PRs are needed.

---

## Remaining Known Gaps

| Gap | Notes |
|---|---|
| `west` not in WSL PATH | Zephyr ZTest suite not run; install west+SDK to enable |
| `qemu_x86` / `qemu_cortex_m3` | QEMU tests require full Zephyr workspace |
| BUGFIXES.md not updated | BUG-A through BUG-G not yet added to `doc/BUGFIXES.md` |

---

*Run by BitConcepts / Oz — 2026-05-31*
