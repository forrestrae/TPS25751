# Plan: BQ25798 Fault-Investigator Example

## Context

A custom 4S Li-ion board built around a BQ25798 charger (input from USB-C PD via a
TPS25751D, typically a 20 V contract) is misbehaving: the STAT-pin LED shows a
distinct 1 Hz flash with erratic flicker between flashes, plus audible inductor
whine. We need a serial-console diagnostic example —
`examples/bq25798-fault-investigator/` — that does more than dump registers: it
reads the BQ25798's status/fault/flag/config/ADC registers through the existing
typed `BQ25798::Device` driver, decodes them, runs heuristic checks, and prints a
structured fault-investigation report pointing at the likely failure mode (active
fault, unstable PD source, ILIM/IINDPM cycling, TS qualification, CE/EN_CHG,
wrong 4S VREG/VSYSMIN, battery OVP/UV, SYS short/OVP, overcurrent,
watchdog/config reset).

Spec: `docs/specs/fault-investigation-example-spec.md`.

### Decisions made with the user (resolving spec vs. codebase tensions)

1. **Read pacing — strict 5 s per read.** The `BQ25798::Device` driver is the only
   read path and every read is a 4CC I2Cr command-task. The TRM requires ≥5 s
   between consecutive same-type commands (the driver *warns* via `DEBUG_CAT_TASK`
   but does not block). Per the user's choice we follow existing examples and
   `delay(5100)` after every read. **Implication:** a full ~35-register sweep takes
   ~3 minutes, NOT the spec's "every 5 seconds." The example will therefore rely on
   the BQ25798's **latched FLAG registers (REG26/27, REG22–25)** to capture
   transient/intermittent faults that occur *between* slow samples — this is the
   correct way to catch a 1 Hz intermittent fault over a link we cannot poll fast.
   The header comment and the periodic banner must state the real sweep cadence.

2. **Read-only only — no setup-write mode.** The typed driver is decode-only (no
   setters; only the inherited raw `TPS25751DownstreamDevice::writeRegister()`).
   Per the user's choice we omit `ENABLE_SAFE_DIAGNOSTIC_SETUP` entirely and keep
   the example purely read-only/non-destructive (matches the `examples/` convention).
   The startup banner prints a one-line note that configuration changes are out of
   scope and would require typed setters not yet in the driver.

3. **Output mode — compile-time flag.** Select verbose vs. CSV via a `#define`
   (e.g. `#define OUTPUT_CSV 0`), matching existing examples (none read serial
   input). No runtime serial-command handling.

## Approach

New example mirroring the existing `bq25798-status` / `bq25798-telemetry` pattern
exactly. Single translation unit (`src/main.cpp`) with grouped helper functions,
as the spec's "Code organization" section requests.

### Files to create / modify

| File | Change |
|---|---|
| `examples/bq25798-fault-investigator/src/main.cpp` | **New** — the sketch (see structure below). |
| `examples/bq25798-fault-investigator/platformio.ini` | **New, optional** — standalone build config; copy verbatim from `examples/bq25798-telemetry/platformio.ini`. |
| `platformio.ini` (library root) | Add `[env:example-bq25798-fault-investigator]` with `build_src_filter = +<*> +<../examples/bq25798-fault-investigator/src>`, mirroring existing `[env:example-bq25798-*]` entries. |
| `examples/README.md` | Add a row to the "Available Examples" table (folder linked, env name, one-line description). |

Follow `examples/AGENTS.md` "Adding a New Example" steps and the Doxygen
`@file/@brief/@section` header from `docs/engineering/DOCUMENTATION.md`.

### `main.cpp` structure

Includes / globals / setup (copy from `bq25798-telemetry/src/main.cpp`):
```cpp
#include <Arduino.h>
#include <TPS25751.h>
#include <BQ25798/BQ25798.h>

const TPS25751  pd;        // host on primary I2Ct bus
BQ25798::Device bq(pd);    // typed BQ25798 driver over 4CC I2Cr tasks
```
- `setup()`: Serial @115200 with the `while (!Serial && millis() < 3000)` wait;
  `TPS25751::setDebugLevel(DEBUG_LEVEL_WARN)`;
  `TPS25751::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_TASK)`; `pd.begin()`.
- **Startup banner** (`printBanner()`): example name; board assumptions (4S,
  VREG 16.8 V, VSYSMIN 12.0 V, expected ~20 V PD VBUS); I2C bus + BQ25798 addr
  (`BQ25798::kDefaultI2CAddress` = 0x6B); **READ-ONLY** mode note + real sweep
  cadence note; output mode (verbose/CSV).
- **Comm check** (`verifyComms()`): `bq.readPartInfo(true)`; on `nullptr` print a
  clear failure message (device not ACKing @0x6B / pull-ups / offset-1 decode); on
  success print PN/DEV_REV. (Validation confirms PN = 011b → BQ25798.)

#### Compile-time config block
```cpp
#define OUTPUT_CSV         0      // 0 = verbose annotated, 1 = compact CSV line
#define HISTORY_DEPTH      4      // rolling snapshots kept for change/event detection
// Board expectations (spec Assumptions)
static const uint16_t kVregExpectedMv    = 16800;
static const uint16_t kVsysminExpectedMv = 12000;
static const uint16_t kVbusPdExpectedMv  = 20000;
static const uint16_t kVbat4sMinMv = 11600, kVbat4sMaxMv = 16800;
```

#### Snapshot type
A POD `Snapshot` struct holding: timestamp (`millis()`), the decoded values we run
heuristics on (raw bytes + key bools/enums + ADC engineering values), and a
"valid" flag per field (so a failed read is distinguishable from zero). Keep a
`Snapshot history[HISTORY_DEPTH]` ring buffer for change detection and the
fault-event block.

#### Registers read each sweep (each via its typed accessor; `delay(5100)` after each)
Use `validate=false` so raw/anomalous data still prints (mirrors `bq25798-status`).
- **Status:** `readChargerStatus0..4` (REG1B–1F) — VBUS/AC present, PG, WD,
  chgStat/vbusStat enums, DPM/thermal/TS state.
- **Fault status (live):** `readFaultStatus0/1` (REG20/21) — all OVP/OCP/short/
  tshut bits.
- **Fault flags (latched):** `readFaultFlag0/1` (REG26/27).
- **Charger flags (latched):** `readChargerFlag0..3` (REG22–25) — incl. poorsrc,
  IINDPM/VINDPM, WD, PG flags.
- **ADC:** `readVbusAdc, readVac1Adc, readVac2Adc, readVbatAdc, readVsysAdc,
  readIbusAdc, readIbatAdc, readTsAdc, readTdieAdc` — engineering units already
  provided by the classes (`millivolts()`, `milliamps()` signed, `percent()`,
  `celsius()`); plus `readAdcControl` to report whether ADC_EN is on.
- **Config sanity:** `readChargeVoltageLimit` (VREG mV), `readChargeCurrentLimit`
  (ICHG mA), `readMinimalSystemVoltage` (VSYSMIN mV), `readInputCurrentLimit`
  (IINDPM mA), `readInputVoltageLimit` (VINDPM mV), `readChargerControl0`
  (enChg/enHiz/enTerm), `readChargerControl1..5` (EN_EXTILIM, EN_IINDPM, watchdog,
  switching frequency), `readTimerControl`, `readNtcControl0/1`.
  > During implementation, use the `bq25798-docs` MCP (`get_register`/
  > `explain_bitfield`) **and the actual header getters** in `include/BQ25798/` to
  > confirm which ChargerControl register exposes EN_EXTILIM / EN_IINDPM /
  > watchdog / switching-frequency before referencing fields. Do not invent getter
  > names — read the headers.

#### Helper functions (spec "Code organization")
- `readSnapshot(Snapshot&)` — performs all reads with pacing, fills the struct.
- `printRawBlock(const Snapshot&)` — hex of key status/fault/config raw bytes
  (use each register's `raw8`/`raw16`/`debugPrint`; show hex alongside decoded).
- `printDecoded(const Snapshot&)` — human-readable status/fault/config fields.
- `printAdc(const Snapshot&)` — labelled ADC values with units + in-range checks
  (reuse the `printMv`-style helper from `bq25798-telemetry`).
- `printCsvLine(const Snapshot&)` — one CSV row (timestamp + key numeric fields +
  fault bitmasks) for logging while reproducing the fault.
- `trackChanges(prev, cur)` — diff latched flags/fault bits; return what's new.
- `runHeuristics(const Snapshot&, const History&)` — the diagnostic checks below.
- `printFaultEvent(...)` — the event block (time, active faults, new flags, V/I/TS
  values, top-3 likely causes).
- `printSummary(...)` — concise, de-duplicated periodic summary (suppress identical
  consecutive messages).

#### Heuristic checks (`runHeuristics`) — implement spec items 1–12
1. **STAT correlation:** active fault bits → "STAT blink explained by active fault
   bits."; else latched flags present → "may be intermittent; inspect latched
   flags/time correlation."; else → "not explained by digital fault registers;
   inspect STAT LED circuit/pullup/sink/noise."
2. **Input path:** compare VBUS vs VAC1/VAC2; flag VBUS < expected PD when 20 V
   contract expected; flag VBUS/VAC OVP; flag IINDPM/VINDPM regulation; detect
   adapter/PD collapse (VBUS droops while IBUS rises / converter cycles — over
   history).
3. **ILIM_HIZ:** report EN_EXTILIM; note actual ILIM = min(pin, IINDPM) when
   external ILIM enabled; warn ILIM_HIZ near HiZ threshold can stop switching;
   print explicit "measure ILIM_HIZ with DMM/scope" instruction (MCU can't read it).
4. **Charge-enable:** report EN_CHG; remind CE pin is active-low and must not
   float; if disabled-but-expected, attribute to register/CE/TS/fault/WD/termination.
5. **TS/thermistor:** decode TS status (cold/cool/warm/hot/suspend); if out of
   charge range say charging suspended by temperature; remind to verify REGN→TS→GND
   NTC divider physically.
6. **Battery / 4S:** check VBAT vs 11.6–16.8 V; warn if VREG ≠ 16.8 V; warn if
   VSYSMIN ≠ 12.0 V; warn VBAT above regulation / below UV; report charge phase
   (precharge/fast/taper/topoff/termination/disabled) from chgStat enum.
7. **SYS rail:** report VSYS; compare to VSYSMIN; flag VSYS short/OVP; if VSYS
   repeatedly dips near VSYSMIN with faults → suggest pulsed/LED load, SYS caps,
   supplement behavior, input power limit.
8. **Overcurrent:** decode IBUS_OCP/IBAT_OCP/CONV_OCP/IBAT-reg; correlate with
   IBUS/IBAT ADC; on CONV_OCP/whine print hardware checklist (inductor sat current,
   SW1/SW2 layout/ringing, PMID/SYS/BAT cap placement, shorted/excess load, LED
   transients, wrong ICHG/IINDPM config).
9. **Poor-source pattern (over history):** VBUS appears/disappears, VBUS droops
   below VINDPM, IBUS spikes/falls, repeating flags → print "possible poor-source /
   PD-contract / input-current-limit cycling."
10. **Watchdog / reset:** detect WD expiration (status/flag); detect config reverting
    to defaults across samples; warn if config appears to reset during the fault cycle.
11. **Event history:** rolling last-N snapshots; on new fault/flag print a fault-event
    block (time, active faults, new flags, VBUS/VAC/VBAT/VSYS/IBUS/IBAT/TS, top-3
    likely causes).
12. **Summary:** every sweep print "No active faults" or "Active faults: ..." plus a
    "Likely cause category: input path / battery path / temperature / overcurrent /
    system rail / configuration / unknown"; suppress identical repeats.

`loop()` = `readSnapshot()` → (verbose: raw + decoded + ADC blocks / CSV: one line)
→ `trackChanges()` → `runHeuristics()` → `printFaultEvent()` if new → `printSummary()`
→ store snapshot in history.

### Error handling
Every read returns `std::unique_ptr<T>`; null = I2C/4CC failure or validation
failure. Mark that field invalid in the `Snapshot` and print `[read failed]`; never
dereference a null pointer. Reuse the `readAndPrint` template idiom from
`bq25798-status/src/main.cpp` for verbose dumps.

## Verification

1. **Compiles (primary acceptance criterion):**
   ```bash
   cd /Users/frae/.../TPS25751-i2ct-test/lib/TPS25751
   pio run -e example-bq25798-fault-investigator
   ```
   Build both `OUTPUT_CSV` settings to confirm both paths compile.
2. **Static review against spec acceptance criteria** (read-only; identifies REG20/21
   active fault bits; prints ADC for VBUS/VAC1/VAC2/VBAT/VSYS/IBUS/IBAT/TS; emits the
   listed likely-cause warnings).
3. **Hardware (if available):** `pio run -e example-bq25798-fault-investigator -t upload`
   then `pio device monitor` @115200 — confirm comm check passes, a full sweep prints,
   and heuristics fire (induce a fault, e.g. TS out of range, to see the event block
   and STAT-correlation message). Note the ~3 min full-sweep cadence from strict 5 s
   pacing.
4. **Confirm registration:** new env appears in `pio project config`; README table row
   present and accurate.

## Out of scope
- Setup/configuration writes (read-only by decision).
- Adding typed setters to `BQ25798::Device` (would belong in the library, not an example).
- Sub-5 s polling / faster sweep (TRM-compliant strict pacing chosen).
