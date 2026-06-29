# Plan: BQ25798 Fault-Investigator — Restore Mode (BQ REG_RST → TPS25751 GAID)

## Context — current task

The restore mode (`RESTORE_CHARGER_DEFAULTS`) is **partly implemented**: it already
issues a BQ25798 `REG_RST` to return the charger to datasheet POR defaults, then
idles. (And the `ENABLE_SAFE_DIAGNOSTIC_SETUP` setup-write feature it undoes is
fully implemented — see "Prior work".)

**Problem this task solves — REG_RST alone is not a true "restore to original".**
On this board the BQ25798 is configured by the **TPS25751**, which boots its own
configuration from an **EEPROM** and acts as the charger's controller over the
I2Cc bus. So the charger's *intended* (original) configuration is whatever the
TPS25751/EEPROM applies — not the BQ25798's chip defaults. To truly restore the
system, the example must also **cold-reset the TPS25751** so it re-reads its EEPROM
and re-applies the BQ25798 configuration.

**Decision (with the user):** expand the existing `RESTORE_CHARGER_DEFAULTS` mode
into one ordered sequence:
1. **BQ25798 `REG_RST`** (clean slate) — must run *first*, while the host is still
   up, because the BQ is reached *through* the TPS25751's 4CC proxy.
2. **TPS25751 GAID cold reset** — `pd.executeCommand(TPS25751FourCC::of("GAID"))`.
   The repo's own docs call `GAID` the destructive "cold reset"; it reboots the
   controller so it re-reads EEPROM and re-applies the BQ25798 config.
3. **Poll until comms return** — after GAID, wait then poll `bq.readPartInfo()`
   (proves the TPS rebooted and is proxying again) with a timeout, then read back a
   BQ25798 config register (e.g. VREG) as evidence the EEPROM config was re-applied.
4. Idle.

### Key API facts (verified)
- `TPS25751::executeCommand(TPS25751FourCC cmd, const uint8_t* inData=nullptr,
  size_t inLen=0, uint8_t* outData=nullptr, size_t outLen=0, uint32_t
  timeoutMs=200) const` — `include/TPS25751.h:307`; `const`, so callable on the
  example's `const TPS25751 pd`. Types come in via `<TPS25751.h>` (includes
  `TPS25751Task.h`). Construct the code with `TPS25751FourCC::of("GAID")`.
- **GAID handshake caveat:** the controller resets mid-command, so `executeCommand`
  may return `TPS25751TaskStatus::Timeout` (COMMAND never clears) even though the
  reset *was* accepted. Treat `Success` **or** `Timeout` as "reset issued"; only a
  `Rejected`/`I2CError` before the device drops is a real failure.
- **Downstream unavailability:** `BQ25798::Device` (`bq`) reaches the charger only
  through `pd`'s I2Cr/I2Cw tasks, so the BQ is unreachable from GAID until the host
  finishes booting — hence the poll-`readPartInfo()` recovery loop.
- BQ `REG_RST` via `updateRegister<BQ25798::TerminationControl>()` is already wired.

### Prior work (already done — do not redo)
- `ENABLE_SAFE_DIAGNOSTIC_SETUP` setup-write feature: gate,
  `applySafeDiagnosticSetup()` with `confirmOrSet*` helpers, three-way banner,
  README row. All build combos pass.
- `RESTORE_CHARGER_DEFAULTS` gate, `restoreChargerDefaults()` doing the BQ
  `REG_RST` + evidence read-backs + loop-idle guard + precedence `#warning`.

Spec: `docs/specs/fault-investigation-example-spec.md`. The example:
`examples/bq25798-fault-investigator/src/main.cpp`.

> **Note — gate name kept.** `RESTORE_CHARGER_DEFAULTS` is retained (not renamed)
> for continuity even though it now also resets the host; its doc comment is
> updated to say so.

> **Hardware caveat to surface in the banner:** GAID drops the PD contract, so
> VBUS may momentarily collapse. If the Teensy is powered from a TPS25751-gated
> rail it could brown-out/reset too. Warn the user.

---

## New work: extend Restore mode to also cold-reset the host

### Approach
The `RESTORE_CHARGER_DEFAULTS` gate, its `setup()` wiring, the `loop()` idle guard,
and the precedence `#warning` are **already implemented**. When set, `setup()`
currently does only the BQ `REG_RST` then idles. This task extends
`restoreChargerDefaults()` so it runs **REG_RST → GAID host cold-reset →
poll-for-recovery**, then idles. Also refresh the gate doc comment / banner / docs
to describe the host reset (they currently say only "REG_RST"). The mode still
takes precedence over `ENABLE_SAFE_DIAGNOSTIC_SETUP`.

### `main.cpp` edits — extend `restoreChargerDefaults()`

The function already does steps (a)–(b). **Add steps (c)–(e)** after the existing
evidence read-backs, plus a couple of tunable constants near the restore gate:
```cpp
static const uint32_t kGaidRecoveryTimeoutMs = 15000;  // max wait for host reboot
static const uint32_t kGaidSettleMs          = 2000;   // initial settle before polling
```

(a) **BQ `REG_RST`** — already implemented (`updateRegister<TerminationControl>`),
   paced. Keep. (Clean slate while the host is still up.)
(b) **Post-REG_RST evidence read-back** — already implemented (`adcEnabled()`,
   `millivolts()`). Keep.
(c) **GAID cold-reset the host** (NEW):
```cpp
Serial.println(F("Cold-resetting TPS25751 (GAID) so it re-reads EEPROM..."));
auto r = pd.executeCommand(TPS25751FourCC::of("GAID"), nullptr, 0, nullptr, 0,
                           /*timeoutMs=*/500);
// Controller resets mid-command; Timeout is expected & not a failure.
const bool issued = (r.status == TPS25751TaskStatus::Success ||
                     r.status == TPS25751TaskStatus::Timeout);
Serial.print(F("  GAID: "));
Serial.println(issued ? F("issued (controller resetting)") : F("REJECTED/ERROR"));
```
(d) **Poll until comms return** (NEW): wait `kGaidSettleMs`, then loop
   `bq.readPartInfo(false)` every `kReadSpacingMs` until non-null or
   `kGaidRecoveryTimeoutMs` elapses; print "host back" / "timed out waiting for
   host". On recovery, read back a BQ config register (`readChargeVoltageLimit()
   ->millivolts()`) as evidence the EEPROM-driven config was re-applied.
(e) **Final message** (UPDATE the existing one): "System restored: BQ25798 reset
   and TPS25751 cold-reset; the controller re-applied the charger config from
   EEPROM."

Steps (a)/(b) print "clean slate" wording; the closing message reflects the full
sequence. Wiring into `setup()` (`#if RESTORE_CHARGER_DEFAULTS … return;`) and the
`loop()` idle guard are **already in place** — no change.

### Banner / docs (UPDATE existing wording)
- Banner RESTORE branch: change "resetting charger to POR defaults, then idle" →
  "RESTORE — REG_RST the charger, then GAID cold-reset the TPS25751 so it re-reads
  EEPROM and re-applies the charger config; then idle." Add the **VBUS-drop / Teensy
  brown-out caveat** line.
- `@section notes` / `@section troubleshooting`: update the `RESTORE_CHARGER_DEFAULTS`
  lines to mention the GAID host reset + EEPROM reconfig (currently they say only
  "REG_RST"). Update the `RESTORE_CHARGER_DEFAULTS` gate doc comment likewise.
- `examples/README.md` row: mention "REG_RST + TPS25751 GAID (re-applies config from
  EEPROM)".

### Out of scope (this task)
- "Restore exact pre-example values" (needs NVM — not chosen).
- Any change to `applySafeDiagnosticSetup()`, the heuristics, or `readSnapshot()`.
- A separate host-reset gate (folded into the one `RESTORE_CHARGER_DEFAULTS` mode).
- Polling `readBootFlags()`/`patchConfigSource()` for recovery — the simpler
  `readPartInfo()` liveness poll is used instead (also proves I2Cc proxying works).

---

## Prior-work reference (already implemented — context only, do not redo)

The setup-write feature lives in `examples/bq25798-fault-investigator/src/main.cpp`:
- Gate `#define ENABLE_SAFE_DIAGNOSTIC_SETUP 0`; targets `kSetupChargeCurrentMa`
  (200 mA), `kSetupInputCurrentMa` (1000 mA); VREG/VSYSMIN reuse
  `kVregExpectedMv`/`kVsysminExpectedMv`.
- `applySafeDiagnosticSetup()` confirm-or-set helpers (`confirmOrSetU16` /
  `confirmOrSetBool`) writing ADC-on, VREG, VSYSMIN, ICHG, IINDPM, OTG-off,
  backup-off via the L3 setters + `updateRegister<>` RMW, each paced
  `delay(kReadSpacingMs)`.
- Three-way-ready banner and the README row already mention the setup mode.

## Verification

1. **Compiles — restore-mode build matrix** (primary acceptance criterion):
   ```bash
   cd /Users/frae/Development/hardware/components/PJRC/examples/TPS25751-i2ct-test/lib/TPS25751
   pio run -e example-bq25798-fault-investigator
   ```
   Build `RESTORE_CHARGER_DEFAULTS` ∈ {0,1} × `ENABLE_SAFE_DIAGNOSTIC_SETUP` ∈ {0,1}
   (OUTPUT_CSV=0):
   - 0/0 default read-only — links no write/reset code.
   - 0/1 setup-write — unchanged, still builds.
   - 1/0 restore-only — `restoreChargerDefaults()` compiles, loop idles.
   - 1/1 — compiles, emits the precedence `#warning`, restore wins.
   Restore all three defines to 0 afterward.
2. **Static review:** with `RESTORE_CHARGER_DEFAULTS=0` no reset/`REG_RST`/GAID code
   is linked; with `=1`, `setup()` runs REG_RST → GAID → poll-recovery in order and
   the loop performs no sweeps/writes; `Timeout` from GAID is treated as success;
   banner states "RESTORE" mode and the VBUS/brown-out caveat.
3. **Hardware (if available):** flash with `RESTORE_CHARGER_DEFAULTS=1`; confirm the
   comm check passes, REG_RST reports OK, GAID prints "issued (controller
   resetting)", the recovery loop reports the host coming back (or a clear timeout),
   the post-recovery VREG read-back shows the EEPROM-applied value, and the sketch
   then idles:
   ```bash
   pio run -e example-bq25798-fault-investigator -t upload && pio device monitor
   ```
   Note: GAID drops the PD contract — expect a VBUS glitch; ensure the Teensy is not
   browned out by it.
4. **README:** the fault-investigator row mentions the REG_RST + GAID restore.
