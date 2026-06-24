# Plan: Load a Patch Bundle Directly to the TPS25751 (PBM)

## Context

The TPS25751 is a USB-C PD controller that normally boots its configuration + firmware
("patch bundle") from an external EEPROM over I2Cc. When no EEPROM is present, the device
powers up in **PTCH** (patch) mode and waits for a host to push the binary image directly
over the I2Ct bus, then transitions to **APP** mode. TI application note **SLVAFV8A**
("Using an Embedded Controller (EC) to Load a Patch Bundle Directly to the TPS25751 or
TPS26750") documents this **Patch Burst Mode (PBM)** flow.

This library currently only *reads* registers — every public method on `TPS25751` is a
`read*`. There is **no write path at all**, so it cannot perform the PBM sequence. This
plan adds the write primitives and a dedicated patch-loader component that implements the
17-step PBM flow, so a Teensy (acting as the EC) can bring an EEPROM-less TPS25751 from
PTCH to APP mode.

**Codebase note (commit `fef7052f`):** The library was just refactored to remove the
`RegisterType` enum — `TPS25751Registers::Address` is now the **single source of truth** for
register identity, and the factory `createRegister(...)` overloads are keyed on `Address`.
This plan aligns naturally: the patch loader is not a register and references registers by
`TPS25751Registers::Address` directly (no `RegisterType`). The `readModeRegister()`
convenience method survives the refactor, so the PTCH/APP mode-check reuse below is valid.

Decisions (confirmed with user):
- **Completion detection:** poll the COMMAND register until the 4CC clears (with timeouts),
  with an **optional** IRQ pin to gate the waits (most faithful to the app note).
- **Scope:** library API (write/burst primitives + `TPS25751PatchLoader`) **plus** a new
  example sketch.
- **Image source:** caller supplies `const uint8_t* image, size_t length`; the example
  ships a placeholder header and documents generating the real low-region C file from TI's
  USBCPD Application Customization Tool (app note §4.2).

## Background: the PBM flow (SLVAFV8A §3–4)

Two I2C access shapes are used:
1. **SMBUS register write** (Table 3-1): `START, addr(W), regNum, byteCount, data…, STOP`.
   Note the **byte-count byte** after the register number — analogous to the length-prefix
   the existing read path already handles at `src/TPS25751.cpp:30-54`.
2. **Raw burst write** to the PBM I2C target address `0x30` (Table 3-3): `START, 0x30(W),
   imageBytes…, STOP` — **no** register number, **no** length byte. Critically (footnote 3):
   the PBM pointer **auto-increments and is NOT reset by I2C START/STOP**, so the image can
   be split across many small transactions. This lets us chunk to Teensy `Wire`'s TX buffer
   instead of needing one giant 4 KB transfer.

The 17 steps (§4.1), translated to this library:
1. (optional) Wait for I2Ct_IRQ low (Ready-for-Patch).
2. Read MODE (`0x03`), confirm `PTCH`.
3. (IRQ mode) Configure INT_MASK1 (`0x16`) for CMD1-complete; clear INT_CLEAR1 (`0x18`).
4. Write PBMs config to DATA (`0x09`): 6 bytes = bundleSize[4, little-endian] + targetAddr
   (`0x30`) + timeout (`0x31` = 3.1 s). `bundleSize == length`.
5. Read DATA (`0x09`) back, confirm first 6 bytes echo the config ("data good").
6. Write COMMAND (`0x08`) = `PBMs` (`0x50 0x42 0x4D 0x73`).
7. Wait for completion (poll CMD and/or IRQ).
8. Read COMMAND (`0x08`), confirm cleared (`0x00…`) and not `!CMD` (rejected/illegal).
9. Read DATA (`0x09`), confirm first byte `PatchStartStatus == 0` (0x04/0x05/0x06 =
   bad bundle size / target / timeout).
10. **Burst** the whole image to `0x30`, chunked (see Wire constraint below).
11. (IRQ mode) Clear INT_CLEAR1 (`0x18`) again.
12. Write COMMAND (`0x08`) = `PBMc` (`0x50 0x42 0x4D 0x63`).
13. Wait for completion.
14. Read COMMAND (`0x08`), confirm cleared / not `!CMD`.
15. Delay 20 ms (let the PD apply the image).
16. Read DATA (`0x09`), confirm `PatchStartStatus` cleared.
17. Read MODE (`0x03`), confirm `APP ` — done.

## Key constraint: Teensy `Wire` TX buffer

Arduino/Teensy `Wire` has a small TX buffer (`BUFFER_LENGTH`, 32 bytes on stock Wire), so a
single `beginTransmission`/`endTransmission` can't carry the full ~11.4 KB image. Because the
PBM pointer survives START/STOP (footnote 3), the burst is split into chunks of
`min(BUFFER_LENGTH - reserve, remaining)` bytes — each chunk is its own
`beginTransmission(0x30) … endTransmission(true)`. Use a single configurable constant
(`PBM_BURST_CHUNK`, default 32 or `BUFFER_LENGTH`) so it can be raised if the project
configures a larger Wire buffer. A small optional inter-chunk settle (the app note uses
500 µs between its 4 KB bursts) is included but tunable.

## Changes

### 1. Write primitives on `TPS25751` (`include/TPS25751.h`, `src/TPS25751.cpp`)

Mirror the existing read primitive (`readRegister(uint8_t, buffer, length)` at
`src/TPS25751.cpp:18`). Add as **public** methods (consistent with the public read API):

- `bool writeRegister(uint8_t regAddr, const uint8_t* data, size_t length) const;`
  SMBUS write: `beginTransmission(_address); write(regAddr); write((uint8_t)length);
  write(data, length); endTransmission(true);`. Reuse `TPS_DEBUG_*` / `TPS_REPORT_I2C_ERROR`
  exactly as the read path does.
- `bool writeRegister(TPS25751Registers::Address addr, const uint8_t* data, size_t length) const;`
  thin overload taking the typed enum (parallels the read overloads at `src/TPS25751.cpp:148-161`).
- `bool burstWrite(uint8_t targetAddr, const uint8_t* data, size_t length, size_t chunk = PBM_BURST_CHUNK, uint16_t interChunkDelayUs = 0) const;`
  raw chunked write (no regNum, no length byte) to `targetAddr` (`0x30`), looping
  `beginTransmission/write/endTransmission(true)` per chunk.

These are the only additions to the core class; the state machine lives in the loader.

### 2. New `TPS25751PatchLoader` (`include/TPS25751PatchLoader.h`, `src/TPS25751PatchLoader.cpp`)

A patch loader is **not** a register, so it deliberately does **not** inherit
`TPS25751Register` or use the factory — note this explicitly in the header doc to avoid a
reviewer expecting the register template. It encapsulates the PBM state machine.

- Constructor: `TPS25751PatchLoader(TPS25751& pd, int irqPin = -1)` (`-1` = pure polling).
- Public:
  - `PatchLoadStatus loadPatchBundle(const uint8_t* image, size_t length);`
  - `void debugPrint(Stream& s = Serial) const;` (reuses `TPS25751Debug`).
  - static `const char* toString(PatchLoadStatus)`.
- `enum class PatchLoadStatus : uint8_t` with one value per failure point so callers get a
  precise reason: `Success, NotInPatchMode, ConfigWriteFailed, ConfigVerifyFailed,
  PbmsRejected, BadBundleSize, BadTargetAddress, BadTimeout, BurstFailed, PbmcRejected,
  PatchApplyFailed, NotInAppMode, CommandTimeout, I2cError`.
- Private helpers (each maps to app-note steps): `waitReadyForPatch()`, `confirmMode(ModeType)`
  (reuse `TPS25751Mode::isPatchMode()/isApplicationMode()` via `pd.readModeRegister()`),
  `clearInterrupts()` / `configInterruptMask()` (only when `irqPin >= 0`), `send4cc(const
  uint8_t cmd[4])` (write DATA if needed + write COMMAND `0x08`), `waitCommandComplete()`
  (poll `0x08` until first byte `0x00`, or detect `!CMD`; honor `irqPin` if set; bounded by a
  timeout using `millis()`), `readPatchStartStatus()` (read DATA `0x09`).
- Constants in the header: `PBM_I2C_TARGET = 0x30`, `PBM_TIMEOUT = 0x31`,
  `CMD_PBMS[]={'P','B','M','s'}`, `CMD_PBMC[]={'P','B','M','c'}`, `PBM_BURST_CHUNK`,
  `PATCH_APPLY_DELAY_MS = 20`, plus command-complete poll timeout. 4CC bytes are the ASCII
  values shown in §4.1 (`PBMs`=`0x50 0x42 0x4D 0x73`, `PBMc`=`…0x63`).
- Reuse existing register addresses from `TPS25751Registers::Address` (`MODE`, `COMMAND`,
  `DATA`, `INT_MASK1`, `INT_CLEAR1`) — all already defined in
  `include/TPS25751RegisterAddress.h:13-41`. No new addresses needed. This is consistent with
  the post-`fef7052f` convention that `Address` is the single register-identity type.

Add a new debug category `DEBUG_CAT_PATCH` (e.g. `0x10`) in `include/TPS25751Debug.h` for the
loader's logging (optional; could reuse `DEBUG_CAT_I2C`/`REGISTER`).

### 3. Wire the loader into the library header

Add `#include "TPS25751PatchLoader.h"` to `include/TPS25751.h` (alongside the other
includes at lines 6-22) so consumers get it transparently.

### 4. New example sketch: `examples/teensy-load-patch/`

Mirror the structure of `examples/teensy-read-registers/` (its own `platformio.ini`,
`src/main.cpp`, `include/`):
- `src/main.cpp`: create `TPS25751 pd;`, `TPS25751PatchLoader loader(pd, I2CT_IRQ_PIN);`,
  in `setup()` call `loader.loadPatchBundle(tps25751_lowRegion_i2c_array, SIZEOFLRB)`,
  print the `PatchLoadStatus`, then read MODE to confirm `APP`.
- `include/patch_bundle.h`: a **placeholder** low-region array
  (`const uint8_t tps25751_lowRegion_i2c_array[] = { … }; constexpr size_t SIZEOFLRB = …;`)
  with a header comment explaining it must be regenerated from TI's USBCPD Application
  Customization Tool → Export → "Generate low region binary" → **C file** format (§4.2,
  Figures 4-12/4-13). On Teensy/ARM flash is memory-mapped, so a plain `const` array needs
  no `PROGMEM`/`pgm_read`.
- `platformio.ini`: copy from the read-registers example (`teensy40`, `lib_deps = lib/TPS25751`).

### 5. Docs

- Edit **`AGENTS.md`** (canonical; `CLAUDE.md` is a symlink to it): add a short "Patch
  loading (PBM)" subsection under Library Architecture pointing at `TPS25751PatchLoader` and
  SLVAFV8A, and update the Architecture component list to mention the new write path.
- Note in the loader header that this is the first **write**/stateful-command path in the
  library (previously read-only).

## Files

Create:
- `include/TPS25751PatchLoader.h`
- `src/TPS25751PatchLoader.cpp`
- `examples/teensy-load-patch/platformio.ini`
- `examples/teensy-load-patch/src/main.cpp`
- `examples/teensy-load-patch/include/patch_bundle.h` (placeholder)

Modify:
- `include/TPS25751.h` — declare `writeRegister`/`burstWrite`; add loader include.
- `src/TPS25751.cpp` — implement `writeRegister`/`burstWrite`.
- `include/TPS25751Debug.h` — add `DEBUG_CAT_PATCH` (optional).
- `AGENTS.md` — document the new capability (canonical; `CLAUDE.md` symlinks to it).

Reuse (do not reinvent):
- I2C debug/error macros: `src/TPS25751.cpp:18-69`, `include/TPS25751Debug.h`.
- Register addresses & sizes: `include/TPS25751RegisterAddress.h` (`COMMAND`, `DATA`,
  `MODE`, `INT_MASK1`, `INT_CLEAR1`).
- Mode decode: `TPS25751Mode::isPatchMode()/isApplicationMode()` via
  `TPS25751::readModeRegister()`.

## Verification

Static / build:
- Build the parent PlatformIO project (per CLAUDE.md "Building & Testing"): from the library
  root, `cd ../..` to `TPS25751-i2ct-test`, then `pio run` (default `teensy40`). The new
  loader sources compile as part of `lib/TPS25751`.
- Temporarily point the parent project at the new example (or build the example) to confirm
  `examples/teensy-load-patch` compiles against the library.

Hardware (EEPROM-less TPS25751 in PTCH mode, Teensy 4.x as EC):
1. Generate a real low-region C file from TI's tool and drop it into `patch_bundle.h`.
2. Flash the example; on serial @115200 confirm: starts in PTCH, `PBMs` accepted
   (`PatchStartStatus == 0`), burst completes, `PBMc` accepted, and final MODE read returns
   `APP `. `PatchLoadStatus::Success` printed.
3. Verify against a logic-analyzer capture matching app-note Figures 4-2…4-11 if available
   (byte-count framing on writes, raw bytes to `0x30`, IRQ assert/clear timing).
4. Negative tests: corrupt the bundle size → expect `BadBundleSize`; run with the device
   already in APP → expect `NotInPatchMode`.

Edge cases to handle in code:
- `length` that isn't a multiple of `PBM_BURST_CHUNK` (final short chunk) — covered by the
  chunk loop using `min(chunk, remaining)`.
- Command timeout: bound every wait with `millis()`-based timeout → `CommandTimeout`.
- `!CMD` rejection detection distinct from the cleared state.
