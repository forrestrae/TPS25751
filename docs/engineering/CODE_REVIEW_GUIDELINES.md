# Code Review Guidelines

This document outlines guidelines for reviewing TPS25751 library code changes, with emphasis on embedded C++ concerns specific to the Teensy 4.x platform and TPS25751 I2C protocol.

---

## Platform-Specific Correctness

The most critical review category for this codebase. Many bugs here cause silent runtime failures or non-compiling builds.

- **RTTI usage**: Any `dynamic_cast<>` or `typeid()` call will **not compile** (`-fno-rtti`). Use `static_cast<>` with factory-based type verification
- **`static_assert(false)` in templates**: Causes compilation errors even for unused template specializations. Use SFINAE/dependent types instead
- **Missing `F()` macro**: String literals passed to `Serial.print/println` without `F()` waste RAM. All string literals in debug output must use `F()`
- **I2C length byte**: The TPS25751 protocol prefixes responses with a byte count. Forgetting to skip byte 0 when parsing register data is a common bug
- **I2C write length byte**: Writes mirror reads — the length byte must be *sent* (`Wire.write((uint8_t)length)` right after the register address), not just expected on reads. A write that omits it will desync the controller's framing
- **PDO unit mismatch**: Fixed-supply and PPS (Programmable Power Supply) PDOs use different voltage/current units. Always verify the conversion factor matches the PDO type
- **Stack usage**: Deep call stacks and large stack-allocated arrays can overflow the limited RAM. Prefer `std::unique_ptr` and heap allocation for buffers > ~64 bytes
- **I2Cr read data offset**: The I2Cr 4CC task's result bytes start at **DATA offset 1**, not 0 — `DATA[0]` is the task return code. Copying from `raw[0]` instead of `raw[1]` silently shifts every byte
- **I2Cw payload length cap**: I2Cw payloads are capped at **11 bytes** (DATA bytes 4-14); bytes beyond DATA byte 14 are silently ignored by the controller, not rejected — verify callers bounds-check `len <= 11` before calling
- **I2Cw `Length` counts the register-offset byte**: The I2Cw `Length` field (`DATA[1]`) is `payload_len + 1`, not `payload_len` — the controller transmits exactly `Length` bytes starting at the offset byte (`DATA[2]`). Setting `Length == payload_len` sends only the offset, a pointer-only write that is ACKed (no NACK) but changes nothing. Note I2Cw `{addr, Length, regOffset, payload}` is **not** symmetric with I2Cr `{addr, regOffset, numBytes}`
- **I2Cr read length cap**: A single I2Cr returns at most **63** data bytes (the 64-byte DATA register reserves `DATA[0]` for the return code); bounds-check `len <= 63`
- **I2Cr/I2Cw target address must be 7-bit**: Bit 7 of the target-address byte in the I2Cr/I2Cw input payload is reserved and must be 0. Passing a raw 8-bit address (e.g. one that already has a R/W bit baked in) corrupts the target device lookup
- **`executeCommand()` 5-second spacing is not enforced by the executor**: The TRM's 5 s minimum spacing between consecutive I2Cr (or consecutive I2Cw) commands is tracked by `TPS25751DownstreamDevice`, not `TPS25751::executeCommand()` itself. Code that calls `executeCommand()` directly with `"I2Cr"`/`"I2Cw"` (bypassing `TPS25751DownstreamDevice`) gets no spacing protection at all
- **Downstream 16-bit registers are big-endian — `extractBits16` byte-swaps them**: `TPS25751BitUtils::extractBits16` assembles `raw[0]` as LSB. Downstream devices (e.g. BQ25798) store 16-bit registers big-endian (`raw[0]` = MSB). Always assemble downstream 16-bit fields manually: `(static_cast<uint16_t>(raw[0]) << 8) | raw[1]`
- **Signed downstream ADC channels**: some downstream ADC values (e.g. BQ25798 IBUS/IBAT/TDIE) are 2's-complement signed. After big-endian assembly, reinterpret as `int16_t` before converting to physical units; treating as `uint16_t` produces wrong sign for negative currents
- **Downstream factory switch completeness**: Every value in `<Device>::Registers::Address` must have a `case` in all three `createRegister()` overloads; a missing `case` silently returns `nullptr` and causes a null-dereference at the call site
- **ADC LSB unit-conversion sourced from datasheet, not MCP**: The register-docs MCP servers do not carry ADC step sizes; conversion factors must come from the device datasheet. Using the wrong LSB produces silently wrong physical values

Reviewing BQ25798 driver code? That driver lives in the sibling `lib/BQ25798` repo —
see its `docs/engineering/CONVENTIONS.md` for the device-specific review traps.

### Patch Burst Mode (PBM) Traps (ADR-011)

Review these when touching `TPS25751PatchLoader`, `TPS25751::burstWrite()`, or
any code that directly implements the SLVAFV8A PBM sequence:

- **`bundleSize` must be little-endian in the 6-byte PBMs config**: bytes 0-3 are
  `length[0..3]` LSB first.  Big-endian encoding silently sends the wrong size,
  causing `PatchStartStatus == 0x04 (BadBundleSize)` with no other indication.
- **The raw burst has NO register number and NO byte-count byte**: `burstWrite()`
  calls `beginTransmission(0x30) / write(data, N) / endTransmission(true)` with
  nothing before the image bytes.  Adding a regNum or length prefix corrupts the
  image at offset 0 and every subsequent byte is shifted.
- **Final short chunk must be bounded correctly**: the loop condition must be
  `thisChunk = min(chunk, remaining)` (not `chunk`), so the last transaction does
  not read past the end of the image array.
- **PatchStartStatus codes must map to distinct `PatchLoadStatus` values**:
  `0x04 → BadBundleSize`, `0x05 → BadTargetAddress`, `0x06 → BadTimeout`.
  Collapsing them all to a single "PBMs failed" hides which config byte was wrong.
- **`0x30` is a 7-bit I2C device address, not a TPS25751 register offset**: pass it
  to `beginTransmission()` / `burstWrite()`; do not confuse it with the
  `TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES` register (which
  coincidentally has the same numeric value as a register *offset*).
- **PBMc timeout must be generous**: `executeCommand(PBMc, …, 5000)` is typical;
  the device may need several seconds to verify the bundle before clearing COMMAND.
  Using the default 200 ms `executeCommand` timeout risks a spurious `CommandTimeout`.
- **Step 16 DATA read-back is a separate call after the delay**: do not use
  `pbmcResult.returnCode` alone to confirm the apply — a separate `readDataRegister()`
  after `delay(PATCH_APPLY_DELAY_MS)` is required (the spec says "read DATA, confirm
  PatchStartStatus cleared").
- **`TPS25751PatchLoader` is not a register class**: do not expect it to inherit
  `TPS25751Register`, implement three-tier validation, or appear in the factory.  It
  is a sequencer; applying register-class review criteria to it is incorrect.

### Host-Register Write-Path Traps (Batch A onward)

Review these when a **host** R/W register class (SLEEP_CONTROL, CUSTOMER_USE, and
the Batch B+ registers) adds field setters or a `TPS25751::writeX()` method:

- **Setters must read-modify-write, never whole-byte assign**: a host setter must
  mask out only its field and OR in the shifted value, preserving reserved and
  sibling bits (`_raw[i] = (_raw[i] & ~fieldMask) | (value << shift)`). A setter
  that assigns the whole byte from one field zeroes reserved bits the controller
  expects unchanged — the same defect class as the downstream RMW trap below.
- **Setters guard `!isValid()`**: a moved-from or default-constructed-then-failed
  register has no buffer; an unguarded setter dereferences null `_raw`.
- **Derive field-position masks, don't restate them**: a `fieldMask` that is
  independently written (e.g. `0x06`) instead of derived (`valueMask << shift`)
  drifts when the class is copied for a field at a different bit position — the
  setter then clears the wrong bits while decode (using shift+width) still reads
  correctly, so set-then-get round-trip tests pass and the corruption is
  write-only. Flag any hand-restated field-position mask.
- **`writeX()` convenience methods and register-class↔address pairing**: until the
  planned typed `writeRegister<T>()` (keyed on a class-level `kAddress`) lands, the
  hand-written `writeSleepControl`/`writeCustomerUse`-style methods pair a class
  with a `RegisterInfo` by hand. The `isValidSize()` guard cannot catch a wrong
  **same-size** pairing (INT_MASK1 0x16 and INT_CLEAR1 0x18 are both
  11 bytes; RECEIVED_SINK_CAPS and TRANSMIT_SINK_CAPS are both 53). Verify each
  `writeX()` names the register whose bytes it actually writes.
- **Firmware data in reserved bits must stay visible**: when hardware shows the
  firmware storing values in TRM-reserved bits and the class drops those checks
  (PD3_CONFIGURATION bits 3:0, every reserved range of GLOBAL_SYSTEM_CONFIGURATION),
  `debugPrint()` must still show those bits, and the `writeX()` Doxygen must say
  to start from a fresh read. Otherwise a write that clears them looks identical
  to one that preserved them. Caught in the Batch H gate.
- **The TRM width is not the wire length**: `RegisterInfo` must match the
  controller's length byte, which can differ from the TRM's bit width rounded up
  (GLOBAL_SYSTEM_CONFIGURATION: 116 bits in the TRM, 14 bytes on hardware). Do not
  sign off a new register's size until a hardware read has confirmed the length byte.

### Downstream-Device Write-Path Traps (ADR-009)

Review these when a downstream R/W register class adds field setters or a driver adds typed writes:

- **Encode must use big-endian `setField16BE` for 16-bit registers**: Mirror the decode side — `setField16BE` writes `raw[0]` as MSB. A little-endian encode helper byte-swaps the value, exactly the inverse of the `extractBits16` decode bug
- **Inverse unit conversion must reuse the class's own decode LSB/offset constant**: An engineering-unit setter (e.g. `setMillivolts`) must invert the *same* `kLsbMv`/`kOffsetMv` the decode accessor uses. A hand-recomputed constant lets encode and decode silently disagree
- **RMW must preserve reserved bits**: Each setter touches only its field's bits via `setField8`/`setField16BE`. Reject any setter that rebuilds the whole register from a subset of fields — it zeroes reserved bits the device expects unchanged
- **Field value masked/clamped to width**: The encode helpers mask the value to the field width; confirm the setter does not pre-shift or pass an out-of-range value expecting a wider field. Offset-based setters must guard unsigned underflow (clamp inputs below the offset to 0)
- **`kAddress` matches the register**: The `static constexpr Registers::Address kAddress` that `writeRegister<T>()`/`updateRegister<T>()` key on must equal the register's actual address; a mismatch writes the right bytes to the wrong register
- **Self-clearing bits read back 0**: REG_RST, WD_RST, FORCE_* are written as `setX(true)` and cleared by hardware. Do not assert they read back 1, and do not loop expecting them to stay set
- **Typed writes obey the I2Cw framing**: `len <= 11` and `Length = payload + 1` (counts the offset byte) apply to typed writes exactly as to raw I2Cw — see the I2Cw bullets above

---

## Hard-to-Notice Bugs

- **Off-by-one in bitfield extraction**: Verify bit positions match the TI datasheet exactly — bit 0 vs bit 1 shifts are easy to transpose
- **Byte order in multi-byte fields**: Confirm endianness matches the target register layout. TPS25751 host registers are little-endian; BQ25798 downstream registers are big-endian. `extractBits16` is always little-endian — do not use it for big-endian downstream registers
- **Reserved bit masking**: Reserved bits must be masked out before comparing or validating field values; raw reads may have non-zero reserved bits set by hardware
- **Integer overflow in bitfield math**: Shifting or multiplying `uint8_t` values before casting to a wider type can overflow; cast first
- **Null pointer on factory miss**: `createRegister()` returns `nullptr` for addresses without a decoder class. Callers must check before dereferencing
- **Validation tier skipping**: `validateData()` must call `validateBasic()` first; `validateSemantic()` must call `validateData()` first. Breaking this chain produces misleading results
- **Move-after-use**: After moving a `std::unique_ptr` into a factory return, the source is null. Ensure moved-from objects are not accessed
- **Incomplete Rule of Five**: If any of copy constructor, copy assignment, move constructor, move assignment, or destructor is user-defined, all five must be considered — missing one leads to double-free or shallow copies
- **Signed/unsigned comparison warnings**: Comparing `int` loop indices against `size_t` lengths can wrap; use consistent types
- **PDO count vs data length mismatch**: In capability registers, verify that `getPDOCount()` * bytes-per-PDO + header never exceeds the actual data buffer size
- **Incomplete transcription of a mirrored register's bit set** *(caught in Batch B)*: When a register mirrors another's layout (INT_MASK1/INT_CLEAR1 mirror INT_EVENT1), a *dropped* named source compiles cleanly and passes every round-trip/preservation test — the missing bit is simply absent, so nothing fails. Spot-checking a few bit offsets will not catch it. **Count the named accessors/setters against the reference class** (e.g. `grep -c extractBits` on both) and reconcile the total, and prefer the granular reference register (INT_EVENT1) over the terser MCP MASK/CLEAR field tables, which may lump maskable bits into "Reserved". Root cause is usually an incomplete bit list in the delegated work order (see plan amendment 3) — the review gate, not the compiler, is the only thing that catches it.
- **PDO byte-offset shift from an embedded header** *(caught in Batch D)*: Do NOT assume every capability register puts PDO 1 at byte 1. TRANSMIT_SOURCE_CAPS (0x32) inserts two Power-Path bytes after the count byte, so PDO 1 starts at **byte 3** (`getPDO`/`setPDO` offset `3 + index*4`), whereas TRANSMIT_SINK_CAPS (0x33) and the Received caps put PDO 1 at byte 1 (`1 + index*4`). Copy-pasting the Received-caps `getPDO` into the source-side class silently reads/writes PDOs two bytes low, corrupting the Power-Path bytes and shifting every PDO. Verify the register's own field table, and unit-test that PDO 0 lands at the expected byte and does not disturb the header bytes.
- **Boolean getter/parameter for a multi-bit field silently truncates** *(caught in Batch D)*: A field wider than 1 bit modeled as `bool` (e.g. the Fast-Role-Swap-Required-Current field, bits 24:23, a 2-bit value 0–3) collapses values ≥1 to `true`/1 and cannot round-trip an encoded value of 2 or 3. On a read-only decoder this merely loses information; on a **R/W** register it makes encode↔decode non-invertible. Model any field of width > 1 as `uint8_t`/`uint16_t`, not `bool`. (This latent defect exists in the shipped read-only `TPS25751ReceivedSinkCaps::getFixedFastRoleSwapCurrent()`, which returns `bool`; the R/W `TPS25751TransmitSinkCaps` returns `uint8_t` instead — see the follow-up note in CONSTRAINTS.md.)
- **`extractBits*()`/`setBits()` silently return 0/no-op past bit 255** *(caught in Batch E)*: These take `uint8_t bitPos`, so any field starting at bit ≥ 256 wraps or is simply unreachable — IO_CONFIG's GPIO12 Mapped Event field starts at bit 384. There is no compiler warning and no runtime assertion; a naive port of the extractBits pattern to a register this wide compiles cleanly and returns 0 for every high-offset field. Registers with fields past bit 255 must assemble bytes directly from `_raw[]` (bounds-checked) instead of calling `extractBits*()` at all — see `TPS25751IOConfig` (mirroring `TPS25751TransmitSourceCaps::getPDO()`'s byte-assembly pattern) and the CONSTRAINTS.md note under Read-Only Registers. Flag any new wide register (> ~31 bytes, i.e. > 248 bits) that still calls `extractBits32`/`extractBits64` for its highest fields without first checking the actual bit offset against 255.
- **`extractBits()` is 8 bits wide; a wider field silently reads 0** *(caught in the Rev. B pass)*: `extractBits()` returns `uint8_t` and rejects `bitLen > 8` by returning 0. Assigning the result to a `uint16_t`/`uint32_t` compiles cleanly, so `extractBits(10, 10)` or `extractBits(0, 32)` looks right and always yields 0. ActiveRDOContract and ActivePDOContract shipped this way from their first version: currents, voltages, power, `activePDO()`, and `hasActiveContract()` all read 0 on hardware. Use `extractBits16` for widths 9-16, `extractBits32` for 17-32, and `extractBits64` above that. The instance `extractBits()` now emits a `TPS_DEBUG_WARN` (category `DEBUG_CAT_PARSING`) when `bitLen > 8`, but that is visible only with warnings compiled in; the review gate is a `grep -n "extractBits(" src/` pass that checks every width argument is 8 or less, plus a known-value test for every multi-bit accessor.

---

## Security Vulnerabilities

For an embedded I2C library, the relevant attack surface is different from web applications:

- **Buffer overflow from I2C data**: Never use the length byte from the wire directly as a buffer index or allocation size without bounds checking against the known maximum register size
- **Integer overflow in size calculations**: `pdoCount * 4` can overflow `uint8_t`; promote to `size_t` before arithmetic
- **Unvalidated enum values from hardware**: Hardware may return out-of-range enum values. Code that indexes arrays or switch statements using raw hardware values without range checks can access out-of-bounds memory
- **Hardcoded I2C addresses**: I2C device addresses should use the named constants in the register map, not magic numbers, to avoid silent address collisions

---

## Standards Compliance

Verify that new code follows all mandatory standards from [STANDARDS.md](STANDARDS.md):

- **Class structure**: Inherits from `TPS25751Register`; implements all pure virtual methods (`getAddress`, `getExpectedSize`, `isReadOnly`, `validateBasic`, `validateData`, `validateSemantic`, `debugPrint`)
- **Three-tier validation**: All three levels implemented and chained correctly
- **Debug print format**: Follows the exact `========================================` header/footer format with `F()` macros
- **Factory integration**: For host registers — address present in `TPS25751Registers::Address` (with `RegisterInfo` size), forward declaration added, all `createRegister()` variants updated with a `case` for the new address. For downstream device registers — address present in `<Device>::Registers::Address`, all `<Device>::RegisterFactoryImpl::createRegister()` variants have a `case`; the host factory is untouched
- **Magic numbers**: Bitfield masks and shift counts defined as `static constexpr` named constants, not inline literals
- **Doxygen**: Every public method has `@brief`, `@return`, and a `@see` reference to the TI datasheet section

---

## Unintended Code

- **Debug print statements left in**: `Serial.print` calls that are not inside `debugPrint()` or guarded by a debug flag
- **Commented-out register fields**: Fields commented out because "not needed yet" — either implement them or document why they're omitted
- **TODO/FIXME without tracking**: Incomplete work must be tracked in the implementation plan, not left as comments
- **Hardcoded test data in production code**: Magic byte arrays that are clearly test vectors
- **Disabled validation**: Validation methods that always `return true` as a placeholder
- **Builder pattern usage**: `TPS25751RegisterBuilder` is deprecated (ADR-001). New code must not use it

---

## Code Style

- **Inconsistent naming**: All register classes use `TPS25751[RegisterName]` prefix; factory enums use `UPPER_SNAKE_CASE`; accessors use `get[FieldName]()`
- **String duplication**: The same string literal appearing in multiple `Serial.print` calls — extract to a helper function or use a lookup table
- **Missing `override` keyword**: All overridden virtual methods must be marked `override` to catch base class signature changes at compile time
- **Raw pointer ownership ambiguity**: If a raw pointer is owning, it must be converted to `std::unique_ptr`. Raw pointers are for non-owning references only
- **Excessive nesting**: Validation logic with more than 2-3 levels of nesting should be flattened with early returns
- **Unused `#include`s**: Unnecessary headers increase compile time and can cause unexpected namespace pollution

---

## Testing

- **Coverage below 90%**: Every register class requires >90% code coverage. New code without tests should be blocked
- **Missing edge case tests**: At minimum, test all-zeros, all-ones, and each field at its maximum valid value
- **Missing negative tests**: Each validation level must have tests for both passing and failing cases
- **Factory creation untested**: Every new register must include factory creation tests (with and without data)
- **Tests that always pass**: Watch for tests with incorrect `EXPECT_*` calls that never actually exercise the failure path
- **A cached build proves nothing about warnings** (caught in Batch G of the rev B plan): `platformio.ini` sets `build_cache_dir = .pio/buildcache`, so `pio run` and even `pio run -t clean` followed by `pio run` retrieve unchanged objects from the cache ("Retrieved ... from cache") and print none of their warnings. A "warning-free" claim needs a compile that bypasses the cache, for example `PLATFORMIO_BUILD_CACHE_DIR=<empty scratch dir> pio run -e example-read-registers` after `-t clean`, with the log showing a `Compiling` line for each new source file

---

## Additional Concerns

- **Performance**: Unnecessary copies of register data (pass `const TPS25751Register*` or `const TPS25751Register&` instead of by value)
- **Backwards compatibility**: Changes to `TPS25751Registers::Address` enum values (not just additions) break binary compatibility with serialized data
- **Missing datasheet reference**: Any bitfield constant or enum value without a `@see` TI TRM section reference makes future verification impossible
- **Hardware test status**: Note in the PR whether changes have been validated on physical TPS25751 hardware