# Plan: Keep the INT_EVENT1 register names and correct their rev B documentation

## Context

Finding 14 in `docs/plans/trm-revB-audit.md` (Appendix A) says the address enumerators `INT_EVENT1`, `INT_MASK1`, and `INT_CLEAR1` carry the rev A "for I2C1" register names, and the audit's "Out of scope" section defers renaming them because it breaks the API. This plan evaluated the rename and decided against it on 2026-11-18. Rev B still uses these short names, so the enumerators are not rev A artifacts. The remaining work corrects the documentation around these three registers, fixes a wrong access type on INT_CLEAR1 found during the review, and aligns the native test build with the Teensy C++ standard. Nothing in it changes register behavior.

Rev B (SLVUCR8B, 2026-08) changed the register titles, not the short names. `list_registers` and `get_register` on the `tps25751-docs` MCP server return:

| Offset | Rev A title (SLVUCR8A) | Rev B title (SLVUCR8B) | MCP name | Page |
|---|---|---|---|---|
| 14h | Interrupt Event for I2C1 | Interrupt Event for I2Ct_IRQ | `INTERRUPT_EVENT_FOR_I2CT_IRQ` | 19 |
| 16h | Interrupt Mask for I2C1 | Interrupt Mask for I2Ct_IRQ | `INTERRUPT_MASK_FOR_I2CT_IRQ` | 21 |
| 18h | Interrupt Clear for I2C1 | Interrupt Clear for I2Ct_IRQ | `INTERRUPT_CLEAR_FOR_I2CT_IRQ` | 23 |

The library never used the rev A titles. It uses TI's short register names, and rev B keeps them. A text extraction of both TRM PDFs in `lib/device-register-mcp-server/devices/tps25751/docs/` gives these counts:

| Term | Rev A | Rev B |
|---|---|---|
| `INT_EVENT1` | 32 | 32 |
| `INT_MASK1` | 0 | 1 |
| `INT_CLEAR1` | 0 | 0 |
| `INT_EVENT`, `INT_MASK`, `INT_CLEAR` without the digit | 0 | 1 each |
| `I2C1` | 27 | 1 |
| `I2Ct_IRQ` | 0 | 19 |

In rev B, the register descriptions themselves use the suffixed names. 14h says "Only the interrupt events enabled in INT_MASK1 (0x16) will be asserted." 16h and 18h each open with "Interrupt ... bit field for INT_EVENT1." Figure 4-1 is titled "Example Sequence for 'GPPI' Task When Host Uses INT_EVENT1", and the MBRd flow reads "Read & clear INT_EVENT1". The unsuffixed forms appear once, in the MODE field description (Table 3-3, p.14), which lists "INT_EVENT (0x14), INT_MASK (0x16), INT_CLEAR (0x18)" as accessible in BOOT and PTCH modes. The digit follows the command interface instance, which rev B still numbers ("Command Register (CMD1) for I2Ct", INT_EVENT1 bit 30 "CMD1 Complete"). `INT_CLEAR1` appears in neither revision; the library extended the pattern from `INT_EVENT1`.

The `Address` enumerators are the library's own short forms, not MCP names. `COMMAND` is TI's CMD1, `DATA` is DATA1, `BOOT_STATUS` is BOOT_FLAGS, `TYPEC_STATE` is TYPE_C_STATE, and `LIQUID_DETECTION_CONFIG` is LIQUID_DETECTION_CONFIGURATION. Class and method names already drop the digit (`TPS25751InterruptMask`, `readInterruptMask()`, `writeInterruptClear()`).

The names are referenced in about 150 lines outside `docs/plans/`, re-checked at 62d5814: `include/TPS25751RegisterAddress.h` (enumerators, `RegisterInfo` constants, two switch tables), `src/TPS25751RegisterFactory.cpp` (nine case labels), `include/TPS25751.h`, `src/TPS25751.cpp`, the Event, Mask, and Clear classes, `include/TPS25751PatchLoader.h`, `src/TPS25751PatchLoader.cpp` (including two `TPS_DEBUG_TRACE` strings), `examples/read-registers/src/main.cpp`, the three interrupt test suites, `AGENTS.md`, and six files in `docs/engineering/`. No method name, class name, or `F()` literal derives from the digit. `../BQ25798` and the parent project's own sources have no references.

The review found three documentation defects next to these names:

- The Mask and Clear headers cite "Table 3-27 family", which matches neither revision (rev B Tables 3-8, 3-9, and 3-10; rev A Tables 3-7, 3-8, and 3-9). The Mask header quotes the rev A sentence "A bit in INT_EVENT1 cannot be set if it is cleared in this register," which rev B removed. The Event header refers to "INTERRUPT_MASK and INTERRUPT_CLEAR registers", names the library does not use.
- The library calls INT_CLEAR1 write-only in eleven places: `include/TPS25751.h:191`, `include/TPS25751InterruptClear.h` lines 11, 20, 29, and 159, `src/TPS25751InterruptClear.cpp:163` (comment) and 134 (the `debugPrint()` header `[write-only]`), `AGENTS.md:267`, `examples/read-registers/src/main.cpp:231`, and the test file comments at `test_TPS25751InterruptClear.cpp` lines 7 and 129. Both TRM revisions mark every 18h field R/W (rev A Table 3-9, rev B Table 3-10, p.23). Rev B marks no field in any register as write-only.
- `docs/engineering/CONSTRAINTS.md` has a "Write-Only Registers (W)" section whose examples are COMMAND and INT_CLEAR1. COMMAND is R/W in rev B (p.16), and `TPS25751::executeCommand()` polls it by reading. The section's "Don't try to read back" usage note contradicts the library's own command executor.

The native test environment compiles at a different C++ standard from the Teensy build. The teensy platform builder passes `-std=gnu++17` to the `example-*` environments (GCC 15.2.1, `toolchain-gccarmnoneeabi-teensy` 1.150201.0). `[env:native]` sets no `-std` flag, so it uses the host default: Apple clang 21 on the development machine defaults to gnu++14 (`__cplusplus` 201402L). Code that compiles on one can fail on the other. The rev B enumerator aliases (`Time1200ms`, `DRP_Src`, and the others) put `[[deprecated]]` on enumerators, a C++17 feature that compiles in gnu++14 only as a compiler extension. On 2026-11-18, `pio test -e native` with `-std=gnu++17` added to `[env:native]` `build_flags` ran in a scratch export of 62d5814. The compile database showed the flag on the library sources, and all 359 test cases across 17 suites passed with no compiler warnings.

## Approach

All commits land on `fix/rev-b-trm`, the branch that carries the rest of the rev B audit work, and ship with it. Each step below is one commit. Run `pio test -e native` after every step. If another session is committing on the branch at the same time, coordinate the order. The audit document in step 4 is the file most likely to conflict.

### 1. Identifiers

Decision: keep `INT_EVENT1`, `INT_MASK1`, and `INT_CLEAR1`.

- MCP and TRM titles (`INTERRUPT_EVENT_FOR_I2CT_IRQ` and so on): rejected. No other enumerator follows the MCP names, and the `_FOR_I2CT_IRQ` suffix encodes the bus name, which is the part TI just changed between revisions and the part most likely to change again.
- Spelled out (`INTERRUPT_EVENT`, `INTERRUPT_MASK`, `INTERRUPT_CLEAR`): rejected. It matches the class names, but no TRM text uses it, so a reader searching the manual for the enumerator finds nothing.
- Unsuffixed short forms (`INT_EVENT`, `INT_MASK`, `INT_CLEAR`): the best target if a rename were wanted. Rev B uses them once (Table 3-3), and dropping the instance digit matches how `COMMAND` and `DATA` treat CMD1 and DATA1.
- Current names: the names rev B uses throughout its prose, including in these three registers' own descriptions. A TRM search for the enumerator lands on the right text.

### 2. Whether to rename

Decision: do not rename. Finding 14's premise does not hold: the enumerators are TI's short names, not the rev A titles, and rev B keeps `INT_EVENT1` and `INT_MASK1` as its working names. A deprecation would warn users for using the name the TRM uses, the rename changes no behavior, and it touches about 150 lines across code, three test suites, and seven docs. The case for renaming is consistency (`COMMAND` and `DATA` drop the digit, `INT_CLEAR1` is a library invention, and rev B prints the unsuffixed forms once). That is a style preference, not a correctness issue.

### 3. Compatibility strategy, recorded for a future rename

Not used by this plan. If a rename is ever chosen, keep the old names as deprecated aliases rather than making a hard break, following `include/TPS25751SleepControl.h:21-22`:

```cpp
/// @deprecated Pre-rename name for 14h. Same value as INT_EVENT.
INT_EVENT1 [[deprecated("SLVUCR8B Rev. B p.19 titles 14h Interrupt Event for I2Ct_IRQ; use Address::INT_EVENT")]] = 0x14,
```

and, in `Registers`, `[[deprecated("use Registers::INT_EVENT")]] constexpr RegisterInfo INT_EVENT1 = INT_EVENT;`. A scratch compile on 2026-11-18 of this pattern in a header was clean with `arm-none-eabi-g++ -std=gnu++17 -Wall -Wextra -fno-rtti` and Apple clang `-Wall -Wextra`, and each use of an old name gave one `-Wdeprecated-declarations` warning on both. The switch tables in `TPS25751RegisterAddress.h` and `TPS25751RegisterFactory.cpp` would list only the new names: an alias has the same value as its replacement, so listing both is a duplicate case value and fails to compile, while omitting the alias still dispatches it by value. A hard break would force a major release for a cosmetic change.

### 4. Release impact

`scripts/release_level.py` gives major for `type!:` or a `BREAKING CHANGE:` footer, minor for any `feat`, and patch otherwise. None of this plan's commits is `feat` or breaking: they are `docs(rev-b)`, `fix(rev-b)`, and `build(native)`, with no `!` and no footer. `library.json` is at 2.0.2, and `fix/rev-b-trm` already carries nine `feat(rev-b)` commits, so the branch releases as 2.1.0 with or without this work. A rename with deprecated aliases would also have been minor (`feat`). A hard break would have given 3.0.0.

### 5. Documentation

Current-state documents describe the library as it is: `AGENTS.md`, `docs/engineering/*.md`, Doxygen, and code comments. They keep the suffixed names and get the corrections in steps 1 and 2. Quotes of TI text stay verbatim.

Plans in `docs/plans/` record past work under the names in use at the time and keep them: `plan-impl-v01.md`, `feature/load-patch-bundle.md`, and `feature/generic-4CC-command-task-executor.md`. Editing them would rewrite history and break their line references. `trm-revB-audit.md` is the tracking document for finding 14, so step 4 adds outcomes to it and leaves the original finding text in place.

### 6. INT_CLEAR1 access type

Describe INT_CLEAR1 as R/W, as both TRM revisions do, and keep the library write-only in practice: no `readInterruptClear()` is added, because reading back staged clears has no use. The comments state the TRM access type and explain why the library only writes it. The `debugPrint()` header changes from `[write-only]` to `[R/W, write 1 to clear]`. That changes program output, so the commit is `fix`, not `docs`. The CONSTRAINTS.md "Write-Only Registers (W)" section is replaced with a short note that the TRM marks no TPS25751 register write-only, that COMMAND and INT_CLEAR1 are R/W registers written to trigger an action, and that INT_CLEAR1 read-back carries no event state. No test asserts the `[write-only]` string.

What a read of 18h returns on hardware is not documented. The plan does not depend on it. A bench read with `example-read-registers` is optional and would be recorded in the audit the way the SLEEP_CONTROL observation was.

### 7. Native C++ standard

Add `-std=gnu++17` to `[env:native]` `build_flags` so host tests compile at the same standard as the Teensy build. Language features then behave the same in both environments, and the enumerator attributes become standard C++ in both instead of an extension in one.

CI already compiles at gnu++17 by accident. Both jobs in `.github/workflows/publish.yml` run on `ubuntu-latest` and install only Python 3.12, so `pio test -e native` uses the runner image's default `g++`, and GCC defaults to gnu++17 from version 11 onward. That makes the development Mac's clang (gnu++14) the outlier. Pinning the flag makes local runs, CI, and the Teensy build use one standard, and keeps CI from silently changing standard if `ubuntu-latest` moves to an image whose GCC has a different default. The exact GCC version comes from the runner image line in a publish run's log and has not been checked.

## Changes

### 1. Correct the interrupt register Doxygen

Commit: `docs(rev-b): cite rev B titles and tables in the interrupt register headers`.

- Files: `include/TPS25751RegisterAddress.h` (a `///` line on each of the three enumerators giving the rev B title, page, and TI short name), `include/TPS25751InterruptEvent.h` (add the rev B title and Table 3-8, p.19; replace "INTERRUPT_MASK and INTERRUPT_CLEAR" with `INT_MASK1` and `INT_CLEAR1`), `include/TPS25751InterruptMask.h` (replace "Table 3-27 family" with Table 3-9, p.21; replace the rev A quote with the rev B description "Interrupt mask bit field for INT_EVENT1. Bytes 1 to 10 of this register must be configured through the Application Customization Tool to enable the corresponding interrupt events."), and `include/TPS25751InterruptClear.h` (replace "Table 3-27 family" with Table 3-10, p.23).
- Tests: comment-only step, so no new test applies. Run the native suite as a regression check.

### 2. Describe INT_CLEAR1 as R/W

Commit: `fix(rev-b): describe INT_CLEAR1 as read/write, as both TRM revisions specify`.

- Files: `include/TPS25751.h:191`, `include/TPS25751InterruptClear.h` (lines 11, 20, 29, and 159; line 29 becomes "Access: R/W (SLVUCR8B p.23); the library only writes it"), `src/TPS25751InterruptClear.cpp` (comment at 163, `debugPrint()` header at 134), `examples/read-registers/src/main.cpp:231` (the sketch skips 18h because writing it is destructive, not because it cannot be read), `test/test_native/test_TPS25751InterruptClear/test_TPS25751InterruptClear.cpp` (comments at 7 and 129), `AGENTS.md:267`, and `docs/engineering/CONSTRAINTS.md` (replace the "Write-Only Registers (W)" section as described in decision 6).
- Tests: no behavior changes. Run the native suite as a regression check.

### 3. Compile the native tests as gnu++17

Commit: `build(native): compile the native tests as gnu++17 to match Teensy`.

- Files: `platformio.ini` (`-std=gnu++17` as the first entry of `[env:native]` `build_flags`), `docs/engineering/TESTING.md` (one bullet in the native-environment list near line 91 stating the standard and why it matches Teensy).
- Tests: `pio test -e native` passes with the flag, confirmed in a scratch export on 2026-11-18 (359 of 359, no warnings). Check the compile database (`pio run -e native -t compiledb`) for `-std=gnu++17` on the library sources.

### 4. Record the outcomes in the rev B audit

Commit: `docs(rev-b): close audit finding 14 and add the INT_CLEAR1 access finding`.

- Files: `docs/plans/trm-revB-audit.md`. Under finding 14, add a resolution: rev B keeps `INT_EVENT1` and `INT_MASK1` as its short names (counts and quotes from this plan's Context), so the enumerators stay, with a pointer to this plan. Replace the "Out of scope" paragraph about the rename with the same pointer. Add finding 16 under "Low priority and no-change items": 18h is R/W in both revisions (rev A Table 3-9, rev B Table 3-10, p.23), the library documented it as write-only, and CONSTRAINTS.md listed COMMAND (R/W, p.16) and INT_CLEAR1 as write-only examples. Add a matching step 16 to the audit's "Changes" with its status tag, files, and the commit from step 2 of this plan. Update the status line.
- Tests: none.

## Files touched

| File | Step | Change |
|---|---|---|
| `include/TPS25751RegisterAddress.h` | 1 | Doxygen on the three enumerators |
| `include/TPS25751InterruptEvent.h` | 1 | Rev B title and table, enumerator names |
| `include/TPS25751InterruptMask.h` | 1 | Rev B table and quote |
| `include/TPS25751InterruptClear.h` | 1, 2 | Rev B table; R/W access |
| `include/TPS25751.h` | 2 | R/W access comment |
| `src/TPS25751InterruptClear.cpp` | 2 | Comment and `debugPrint()` header |
| `examples/read-registers/src/main.cpp` | 2 | Comment |
| `test/test_native/test_TPS25751InterruptClear/test_TPS25751InterruptClear.cpp` | 2 | Comments |
| `AGENTS.md` | 2 | R/W access |
| `docs/engineering/CONSTRAINTS.md` | 2 | Replace "Write-Only Registers (W)" |
| `platformio.ini` | 3 | `-std=gnu++17` for `[env:native]` |
| `docs/engineering/TESTING.md` | 3 | Native standard |
| `docs/plans/trm-revB-audit.md` | 4 | Finding 14 resolution, finding and step 16, "Out of scope", status |
| `docs/plans/plan-impl-v01.md`, `docs/plans/feature/*.md` | | Unchanged (history) |

## Verification

1. `pio test -e native` passes after every step.
2. Every `example-*` environment builds. List them with `pio project config`; today they are `pio run -e example-read-registers` and `pio run -e example-load-patch-bundle`.
3. Grep for leftovers:
   - `git grep -n 'Table 3-27' -- include` prints nothing.
   - `git grep -n 'cannot be set if it is' -- include` prints nothing.
   - `git grep -n -i 'write-only' -- include src examples test AGENTS.md docs/engineering` prints nothing about INT_CLEAR1 or COMMAND. The one expected hit is `docs/engineering/CODE_REVIEW_GUIDELINES.md:82`, which uses "write-only" for a class of setter bug, not a register.
   - The old names appear in the same files as before step 1, because they are intentionally unchanged. Compare the file lists at the plan commit and at the branch tip: `diff <(git grep -l -E 'INT_(EVENT|MASK|CLEAR)1' 6836a27 -- . ':!docs/plans/' | cut -d: -f2-) <(git grep -l -E 'INT_(EVENT|MASK|CLEAR)1' HEAD -- . ':!docs/plans/' | cut -d: -f2-)` prints nothing (23 files at both). Do not add `\b` to the pattern: `git grep -E` does not support it, so the pattern then matches nothing and the check passes vacuously.
4. `scripts/verify.py` passes on `fix/rev-b-trm` once the branch is rebased onto `main` with a clean tree, untracked files included.
