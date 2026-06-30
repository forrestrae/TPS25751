# Plan: Load a Patch Bundle Directly to the TPS25751 (PBM)

## Context

The TPS25751 is a USB-C PD controller that normally boots its configuration + firmware
("patch bundle") from an external EEPROM over I2Cc. When no EEPROM is present, the device
powers up in **PTCH** (patch) mode and waits for a host to push the binary image directly
over the I2Ct bus, then transitions to **APP** mode. TI application note **SLVAFV8A**
("Using an Embedded Controller (EC) to Load a Patch Bundle Directly to the TPS25751 or
TPS26750") documents this **Patch Burst Mode (PBM)** flow.

This feature builds on the library's existing write + 4CC infrastructure; it adds only the
one PBM-specific access shape and the PBM sequencing on top. Reused as-is:

- **Write path** — `TPS25751::writeRegister(TPS25751Registers::Address, const uint8_t*, size_t)`
  (public, `src/TPS25751.cpp:191`) and the raw `writeRegister(uint8_t regAddr, …)` SMBUS
  primitive (`src/TPS25751.cpp:164`), which already emits the byte-count byte after the
  register number.
- **Generic 4CC executor** — `TPS25751::executeCommand(cmd, inData, inLen, outData, outLen,
  timeoutMs)` (`include/TPS25751.h:307`, `src/TPS25751.cpp:480`) performs the full
  DATA-write → COMMAND-write → poll → DATA-read-back handshake, and `waitForCommandClear()`
  (`src/TPS25751.cpp:451`) does the COMMAND polling incl. `"!CMD"` rejection + timeout.
  `executeCommand` stages short DATA payloads via the private raw overload (exact `inLen`),
  so a 6-byte config write into the 64-byte DATA register is not rejected.
- **Task value types** — `TPS25751FourCC`, `TPS25751TaskStatus`
  (`Success`/`Rejected`/`Timeout`/`I2CError`), and `TPS25751TaskResult{status, returnCode}`
  in `include/TPS25751Task.h`.

So the loader issues `PBMs` and `PBMc` through `executeCommand()` and adds only (a) the **PBM
raw burst write** to target `0x30` — the one access shape `executeCommand` does *not* cover —
and (b) the PBM-specific sequencing, mode checks, and status mapping.

`TPS25751Registers::Address` is the single source of truth for register identity; the loader
references registers by `Address` directly. `readModeRegister()` is reused for the PTCH/APP
mode checks.

Decisions (confirmed with user):
- **Completion detection:** `executeCommand()`'s built-in COMMAND polling (timeout + `"!CMD"`
  detection) is the mechanism; the loader does not reimplement polling. An **optional** IRQ
  pin remains a loader-level convenience for gating *between* commands, but
  `executeCommand`/`waitForCommandClear` are polling-only internally (an IRQ-driven variant is
  deferred — see ADR-007).
- **Scope:** library API (one new `burstWrite` primitive + `TPS25751PatchLoader`) plus a new
  example sketch.
- **Image source:** caller supplies `const uint8_t* image, size_t length`; the example ships a
  placeholder header and documents generating the real low-region C file from TI's USBCPD
  Application Customization Tool (app note §4.2).

## Background: the PBM flow (SLVAFV8A §3–4)

Two I2C access shapes are used:
1. **SMBUS register write** (Table 3-1): `START, addr(W), regNum, byteCount, data…, STOP`.
   Note the **byte-count byte** after the register number — analogous to the length-prefix the
   existing read path handles at `src/TPS25751.cpp:30-54`.
2. **Raw burst write** to the PBM I2C target address `0x30` (Table 3-3): `START, 0x30(W),
   imageBytes…, STOP` — **no** register number, **no** length byte. Critically (footnote 3):
   the PBM pointer **auto-increments and is NOT reset by I2C START/STOP**, so the image can be
   split across many small transactions. This lets us chunk to Teensy `Wire`'s TX buffer
   instead of needing one giant 4 KB transfer.

The 17 steps (§4.1), translated to this library. Steps 4–9 and 12–14 are each a single
`executeCommand()` call — DATA staging, COMMAND write, completion poll, `"!CMD"` detection, and
DATA read-back are all inside the executor; the loader supplies the payload and interprets
`TPS25751TaskResult`:

1. (optional) Wait for I2Ct_IRQ low (Ready-for-Patch).
2. Read MODE (`0x03`), confirm `PTCH` — via `pd.readModeRegister()`.
3. (IRQ mode, optional) Configure INT_MASK1 (`0x16`) for CMD1-complete; clear INT_CLEAR1 (`0x18`).
4–9. **`executeCommand(PBMs, config, 6, &startStatus, 1, timeout)`**, where `config` = 6 bytes
   = bundleSize[4, little-endian] + targetAddr (`0x30`) + timeout (`0x31` = 3.1 s),
   `bundleSize == length`. Inspect the result:
   - `result.status == Rejected` → `PbmsRejected` (controller wrote `"!CMD"`).
   - `result.status == Timeout`/`I2CError` → `CommandTimeout`/`I2cError`.
   - `result.returnCode` is `PatchStartStatus` (DATA byte 0): `0` = good; `0x04`/`0x05`/`0x06`
     = bad bundle size / target / timeout → `BadBundleSize`/`BadTargetAddress`/`BadTimeout`.
10. **Burst** the whole image to `0x30`, chunked (see Wire constraint below) — `pd.burstWrite()`.
11. (IRQ mode, optional) Clear INT_CLEAR1 (`0x18`) again.
12–14. **`executeCommand(PBMc, nullptr, 0, nullptr, 0, timeout)`** — no input payload; map
    `Rejected` → `PbmcRejected`, `Timeout`/`I2CError` accordingly.
15. Delay 20 ms (let the PD apply the image).
16. Read DATA (`0x09`), confirm `PatchStartStatus` cleared — `pd.readDataRegister()`.
17. Read MODE (`0x03`), confirm `APP ` — done.

## Key constraint: Teensy `Wire` TX buffer

Arduino/Teensy `Wire` has a small TX buffer (`BUFFER_LENGTH`, 32 bytes on stock Wire), so a
single `beginTransmission`/`endTransmission` can't carry the full ~11.4 KB image. Because the
PBM pointer survives START/STOP (footnote 3), the burst is split into chunks of
`min(BUFFER_LENGTH - reserve, remaining)` bytes — each chunk is its own
`beginTransmission(0x30) … endTransmission(true)`. Use a single configurable constant
(`PBM_BURST_CHUNK`, default 32 or `BUFFER_LENGTH`) so it can be raised if the project
configures a larger Wire buffer. A small optional inter-chunk settle (the app note uses 500 µs
between its 4 KB bursts) is included but tunable.

## Changes

### 1. One new primitive on `TPS25751`: `burstWrite` (`include/TPS25751.h`, `src/TPS25751.cpp`)

The only new core primitive is the **raw PBM burst** — a write to a *different* I2C address
(`0x30`) with **no** register number and **no** length byte, which no existing method covers
(the loader reaches DATA/COMMAND through `executeCommand()`, not the write primitives directly):

- `bool burstWrite(uint8_t targetAddr, const uint8_t* data, size_t length, size_t chunk = PBM_BURST_CHUNK, uint16_t interChunkDelayUs = 0) const;`
  raw chunked write (no regNum, no length byte) to `targetAddr` (`0x30`), looping
  `beginTransmission/write/endTransmission(true)` per chunk. Reuse `TPS_DEBUG_*` /
  `TPS_REPORT_I2C_ERROR` exactly as `writeRegister` does. Make it **public** (consistent with
  the public read/write API).

This is the only addition to the core class; everything else lives in the loader.

### 2. New `TPS25751PatchLoader` (`include/TPS25751PatchLoader.h`, `src/TPS25751PatchLoader.cpp`)

A patch loader is **not** a register, so it deliberately does **not** inherit `TPS25751Register`
or use the factory — note this explicitly in the header doc to avoid a reviewer expecting the
register template. It encapsulates the PBM state machine on top of `executeCommand()`.

- Constructor: `TPS25751PatchLoader(TPS25751& pd, int irqPin = -1)` (`-1` = pure polling).
- Public:
  - `PatchLoadStatus loadPatchBundle(const uint8_t* image, size_t length);`
  - `void debugPrint(Stream& s = Serial) const;` (reuses `TPS25751Debug`).
  - static `const char* toString(PatchLoadStatus)`.
- `enum class PatchLoadStatus : uint8_t` with one value per failure point so callers get a
  precise reason: `Success, NotInPatchMode, PbmsRejected, BadBundleSize, BadTargetAddress,
  BadTimeout, BurstFailed, PbmcRejected, PatchApplyFailed, NotInAppMode, CommandTimeout,
  I2cError`. (No separate `ConfigWriteFailed`/`ConfigVerifyFailed`: DATA staging is inside
  `executeCommand()` and surfaces as `I2cError`, and the config is validated by the
  `PatchStartStatus` return code rather than a config-echo read.) A small private helper maps
  `TPS25751TaskStatus` → `PatchLoadStatus` so both the `PBMs` and `PBMc` sites translate the
  executor result the same way (`Rejected` → caller-supplied rejected enum, `Timeout` →
  `CommandTimeout`, `I2CError` → `I2cError`).
- Private helpers (each maps to app-note steps): `waitReadyForPatch()`, `confirmMode(ModeType)`
  (reuse `TPS25751Mode::isPatchMode()/isApplicationMode()` via `pd.readModeRegister()`),
  `clearInterrupts()` / `configInterruptMask()` (only when `irqPin >= 0`).
- Constants in the header: `PBM_I2C_TARGET = 0x30`, `PBM_TIMEOUT = 0x31`, the two 4CC codes as
  `constexpr TPS25751FourCC CMD_PBMS = TPS25751FourCC::of("PBMs");` /
  `CMD_PBMC = TPS25751FourCC::of("PBMc");` (reuse the value type rather than raw `uint8_t[4]`),
  `PBM_BURST_CHUNK`, `PATCH_APPLY_DELAY_MS = 20`, plus the `executeCommand` timeout to pass
  through.
- Reuse existing register addresses from `TPS25751Registers::Address` (`MODE`, `INT_MASK1`,
  `INT_CLEAR1`; `COMMAND`/`DATA` are touched only via `executeCommand`) — all already in
  `include/TPS25751RegisterAddress.h`. No new addresses needed.

Logging: reuse the existing `DEBUG_CAT_TASK` (`0x10`) category — the PBM flow *is* a 4CC task
sequence, and `executeCommand`/`waitForCommandClear` already log under it. (A new
`DEBUG_CAT_PATCH` would collide with `DEBUG_CAT_TASK` at `0x10`; none is needed.)

### 3. Wire the loader into the library header

Add `#include "TPS25751PatchLoader.h"` to `include/TPS25751.h` (alongside the other includes at
lines 6-25) so consumers get it transparently.

### 4. New example sketch: `examples/load-patch-bundle/`

Follow the **current** example convention (e.g. `examples/bq25798-direct-i2c/`): the example
folder holds only `src/`, and the build environment is added to the **library root
`platformio.ini`** as `[env:example-load-patch-bundle]` with
`build_src_filter = +<*> +<../examples/load-patch-bundle/src>`. Examples no longer carry a
per-folder `platformio.ini` (the one left in `read-registers/` is legacy) and no `teensy-`
prefix. Files:
- `src/main.cpp`: create `TPS25751 pd;`, `TPS25751PatchLoader loader(pd, I2CT_IRQ_PIN);`, in
  `setup()` call `loader.loadPatchBundle(tps25751_lowRegion_i2c_array, SIZEOFLRB)`, print the
  `PatchLoadStatus` via `TPS25751PatchLoader::toString()`, then read MODE to confirm `APP`.
- `src/patch_bundle.h`: a **placeholder** low-region array
  (`const uint8_t tps25751_lowRegion_i2c_array[] = { … }; constexpr size_t SIZEOFLRB = …;`) with
  a header comment explaining it must be regenerated from TI's USBCPD Application Customization
  Tool → Export → "Generate low region binary" → **C file** format (§4.2, Figures 4-12/4-13). On
  Teensy/ARM flash is memory-mapped, so a plain `const` array needs no `PROGMEM`/`pgm_read`.
- Add `[env:example-load-patch-bundle]` to the library root `platformio.ini` (one stanza,
  mirroring the existing `example-*` envs).

### 5. Docs

Update every documentation file the feature touches (canonical sources only — `CLAUDE.md`
files are symlinks to their `AGENTS.md`, so edit the `AGENTS.md`):

- **`AGENTS.md`** (root, canonical): add a "Patch loading (PBM)" subsection under *Library
  Architecture* pointing at `TPS25751PatchLoader` and SLVAFV8A, and note it as a second consumer
  of the 4CC executor (alongside `TPS25751DownstreamDevice`). Add `TPS25751PatchLoader` to the
  Core Components list. Also fix the now-stale *Building & Testing Your Changes* section: the
  library **does** now have a root `platformio.ini` with `example-*` envs (it currently claims
  there is none) — update it to the `pio run -e example-<name>` workflow used by
  `examples/README.md`.
- **`docs/engineering/ARCHITECTURE.md`**: add **ADR-011** ("Patch loader — a non-register PBM
  sequencer on `executeCommand()`"), recording that the loader deliberately is *not* a register
  (no `TPS25751Register` base, no factory) and that the only new core primitive is the raw
  `burstWrite` to `0x30`. Follow the existing ADR-007…ADR-010 format. Add `TPS25751PatchLoader`
  to the component overview if components are enumerated there.
- **`docs/engineering/CONSTRAINTS.md`**: add a "Patch Burst Mode (PBM)" subsection under
  *I2C Protocol Details* (after the existing "4CC Command Interface" section, ~line 262)
  documenting the raw `0x30` burst shape (no register number, no byte-count byte — unlike every
  other write), the **PBM pointer auto-increments and survives I2C START/STOP** (so the image
  may be chunked), and the Teensy `Wire` TX-buffer chunking constraint.
- **`docs/engineering/CODE_REVIEW_GUIDELINES.md`**: add the PBM-specific review traps so the
  Opus `/code-review` gate (see *Implementation workflow* below) has a checklist: little-endian
  `bundleSize` in the 6-byte `PBMs` config; `Length`/byte-count framing differs for the raw
  burst (none) vs. SMBUS writes; `min(chunk, remaining)` final-short-chunk handling;
  `PatchStartStatus` codes (`0x04/0x05/0x06`) mapped to distinct `PatchLoadStatus` values; the
  `0x30` target address is a raw 7-bit target, not a TPS25751 register.
- **`examples/README.md`**: add a row for `load-patch-bundle` / `example-load-patch-bundle` to
  the *Available Examples* table, describing it as the host-side PBM patch-load path.
- **`README.md`** (root): add a one-line *Highlights* bullet for patch-bundle loading (the
  EEPROM-less PTCH→APP bring-up path); it already defers example enumeration to
  `examples/README.md`, so no example list change is needed there.
- Loader **header doc**: note it is built **on top of `executeCommand()`** (the generic 4CC task
  interface, TRM Sec 4.4.2) and only adds the PBM raw-burst write + sequencing.

No change needed to `STANDARDS.md` / `DOCUMENTATION.md` / `TESTING.md` (they govern register
class structure and the not-yet-existing unit-test harness; the loader is neither a register nor
covered by tests yet) or to `docs/plans/plan-impl-v01.md` (the register roadmap — PBM adds no
register).

## Files

Create:
- `include/TPS25751PatchLoader.h`
- `src/TPS25751PatchLoader.cpp`
- `examples/load-patch-bundle/src/main.cpp`
- `examples/load-patch-bundle/src/patch_bundle.h` (placeholder)

Modify:
- `include/TPS25751.h` — declare `burstWrite`; add loader include.
- `src/TPS25751.cpp` — implement `burstWrite`.
- `platformio.ini` (library root) — add `[env:example-load-patch-bundle]`.
- `AGENTS.md` (root, canonical) — document the new capability + fix the stale build section.
- `docs/engineering/ARCHITECTURE.md` — ADR-011.
- `docs/engineering/CONSTRAINTS.md` — "Patch Burst Mode (PBM)" subsection.
- `docs/engineering/CODE_REVIEW_GUIDELINES.md` — PBM review traps.
- `examples/README.md` — new example row.
- `README.md` — patch-loading Highlights bullet.

Reuse (do not reinvent):
- **4CC executor**: `TPS25751::executeCommand()` for the `PBMs`/`PBMc` handshake; the
  `TPS25751FourCC` / `TPS25751TaskStatus` / `TPS25751TaskResult` value types
  (`include/TPS25751Task.h`).
- **Write primitives**: `TPS25751::writeRegister(Address, …)` (public) and the raw SMBUS
  primitive (already emit the byte-count byte).
- I2C debug/error macros: `include/TPS25751Debug.h` (`DEBUG_CAT_TASK`, `TPS_REPORT_I2C_ERROR`).
- Register addresses: `include/TPS25751RegisterAddress.h` (`MODE`, `INT_MASK1`, `INT_CLEAR1`).
- Mode decode: `TPS25751Mode::isPatchMode()/isApplicationMode()` via
  `TPS25751::readModeRegister()`.

## Implementation workflow (delegate-implement, Opus quality gate)

This feature is a good fit for delegated implementation with an Opus review gate: it is a
single, well-bounded surface (one ~20-line primitive, one self-contained state-machine class,
one example, and mechanical doc edits) that is fully specified above, yet it carries several
embedded-C++ correctness traps (chunking, little-endian `bundleSize`, byte-count vs. raw-burst
framing, status-code mapping). That shape — bounded + specified + trap-laden — is exactly where
a cheaper implementer plus a focused reviewer pays off.

1. **Implement (Sonnet subagent).** Hand this plan to a Sonnet subagent to produce the full diff
   — all *Create*/*Modify* files, including the doc updates in §5. The subagent must follow this
   plan exactly (design decisions here are fixed, not open for re-litigation) and run
   `pio run -e example-load-patch-bundle` to confirm it builds before reporting back.
2. **Quality gate (Opus + `/code-review`).** On the returned working-tree diff, Opus runs
   `/code-review high`, focusing on the PBM traps now listed in `CODE_REVIEW_GUIDELINES.md`
   (little-endian config, framing differences, final-short-chunk bound, `PatchStartStatus`
   mapping, 7-bit `0x30` target, `DEBUG_CAT_TASK` reuse). Use `--fix` or address findings
   directly; for larger gaps, send the Sonnet subagent back via `SendMessage` (preserving its
   context) rather than respawning.
3. **Re-gate** after fixes until `/code-review high` is clean and the build passes; only then is
   the change ready for the human hardware verification below.

Note: `/code-review high` runs locally and is launchable by the orchestrating Opus session
(unlike the billed, user-only `/code-review ultra`). Hardware validation stays with the human.

## Verification

Static / build (from the library root, using the root `platformio.ini` example envs):
- `pio run -e example-load-patch-bundle` — compiles the new loader sources (part of
  `lib/TPS25751`) together with the example `src/`. This is the same workflow
  `examples/README.md` documents for every other example.
- Spot-check an existing env (e.g. `pio run -e example-read-registers`) still builds, to confirm
  the new `burstWrite` declaration / loader include didn't break the shared headers.

Hardware (EEPROM-less TPS25751 in PTCH mode, Teensy 4.x as EC):
1. Generate a real low-region C file from TI's tool and drop it into `patch_bundle.h`.
2. Flash the example; on serial @115200 confirm: starts in PTCH, `PBMs` accepted
   (`PatchStartStatus == 0`), burst completes, `PBMc` accepted, and final MODE read returns
   `APP `. `PatchLoadStatus::Success` printed.
3. Verify against a logic-analyzer capture matching app-note Figures 4-2…4-11 if available
   (byte-count framing on writes, raw bytes to `0x30`, IRQ assert/clear timing).
4. Negative tests: corrupt the bundle size → expect `BadBundleSize`; run with the device already
   in APP → expect `NotInPatchMode`.

Edge cases to handle in code:
- `length` that isn't a multiple of `PBM_BURST_CHUNK` (final short chunk) — covered by the chunk
  loop using `min(chunk, remaining)` in `burstWrite`.
- Command timeout / `!CMD` rejection: handled inside `executeCommand()` /
  `waitForCommandClear()` — the loader just maps `TPS25751TaskStatus::Timeout` → `CommandTimeout`
  and `::Rejected` → `Pbm{s,c}Rejected`. No `millis()` polling in the loader itself.
