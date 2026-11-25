# Code review guidelines

A checklist for reviewing changes to the TPS25751 library. Most entries record a defect
that reached review or hardware in this repository. Each one compiles cleanly and passes
a round-trip test, so the review is the only gate that catches it. The rules themselves
are in [STANDARDS.md](STANDARDS.md) and [CONSTRAINTS.md](CONSTRAINTS.md).

## Platform

- `dynamic_cast` and `typeid` do not compile under `-fno-rtti`.
- `static_assert(false, ...)` in a template body fails even when the template is unused.
- A string literal passed to a `Stream` without `F()` costs RAM.
- A buffer larger than about 64 bytes does not belong on the stack.

## Bus framing

- The first byte of a register read is the length byte. A write must send one after the
  register number, or the controller's framing desynchronizes.
- Never use a length byte from the wire as an index or allocation size without checking
  it against the register's known size.
- `I2Cr` results start at `DATA[1]`, and one `I2Cr` returns at most 63 bytes.
- The `I2Cw` `Length` field is the payload length plus 1, because it counts the
  register-offset byte. A `Length` equal to the payload length sends only the offset: the
  write is acknowledged and changes nothing.
- An `I2Cw` payload is at most 11 bytes. The controller drops the excess without an
  error, so the caller must check.
- The `I2Cr` and `I2Cw` target address is 7 bits. A raw 8-bit address corrupts the lookup.
- `executeCommand()` does not enforce the TRM's 5 s spacing between two `I2Cr` or two
  `I2Cw` commands. Only `TPS25751DownstreamDevice` tracks it.

## Bit fields

- Check every bit position and width against the TRM table, not against a neighbouring
  accessor.
- `extractBits()` returns `uint8_t` and reads 0 for a width above 8. Assigning it to a
  wider type compiles and always yields 0; `TPS25751ActivePDOContract` and
  `TPS25751ActiveRDOContract` shipped that way. Use `extractBits16`, `extractBits32`, or
  `extractBits64`, and check with `grep -n "extractBits(" src/` that every width is 8 or
  less.
- A bit position held in `uint8_t` wraps past bit 255. Keep computed positions in
  `uint16_t` or wider (ADR-012).
- A multi-bit field modeled as `bool` truncates. The 2-bit Fast Role Swap current read as
  `bool` until 6627be1.
- A register that mirrors another's bit map can drop a named source and still pass every
  test, because the missing bit is simply absent. For INT_MASK1 and INT_CLEAR1, count the
  accessors against INT_EVENT1 and reconcile the totals.
- Cast before shifting or multiplying a `uint8_t`. `pdoCount * 4` overflows in 8 bits.
- A raw enum value from hardware can be out of range. Do not index or switch on it
  without the clamp that `extractEnum*()` provides.

## Register classes

- The class inherits `TPS25751Register` directly, has the four constructors of the
  reference classes, and overrides only `isSemanticallyValid()` and `debugPrint(Stream&)`.
- `isSemanticallyValid()` starts with the base checks and the size check, and accepts the
  documented reset value.
- Every field is read through `extractBits*()` or `extractEnum*()` and written through
  `setBits()`. No indexing of `_raw[]` and no restated field mask.
- A new `Address` needs a case in both `getRegisterSize()` and `getRegisterInfo()`. Both
  switches end in `default`, so the compiler reports nothing. A missing
  `getRegisterInfo()` case makes `readRegisterByAddress()` return null while the typed
  read still works; CUSTOMER_USE lacked it until a bench walk over all 37 addresses.
- All three `createRegister()` switches need the case. A missing one returns `nullptr`.
- The size in `RegisterInfo` must match the controller's length byte, which can differ
  from the TRM width. GLOBAL_SYSTEM_CONFIGURATION is 116 bits in the TRM and 14 bytes on
  hardware. Do not sign off a size before a hardware read.
- `debugPrint()` prints to the stream it is given, uses the banner format, and uses
  `printHex()` for fixed-width hex.
- Every public method cites the TRM document, table, page, and bit range, and the USB PD
  table for a specification field.

## Write paths

- Check the TRM access type before adding a write path, and confirm it on hardware with a
  read-back. The controller acknowledges a write to read-only CUSTOMER_USE and keeps the
  old value; the library shipped `writeCustomerUse()` until 2026-11-19.
- A reserved-bit check must be confirmed on hardware. The firmware sets TRM-reserved bits
  in PD3_CONFIGURATION and GLOBAL_SYSTEM_CONFIGURATION, and a check on them rejects every
  valid read.
- Where the firmware keeps data in reserved bits, `debugPrint()` must still show them and
  the `write<Name>()` comment must say to start from a fresh read. Otherwise a write that
  clears them looks the same as one that kept them.
- A `write<Name>()` method forwards to `writeRegister<T>()`, which takes the address from
  the class's `kAddress`. Check that `kAddress` names the class's own register.

## USB PD decode

- PDO 1 is not at byte 1 in every capability register. TRANSMIT_SOURCE_CAPS (0x32) puts
  it at byte 3.
- PPS fields use 100 mV and 50 mA; the other PDO types use 50 mV and 10 mA. Not every
  Augmented PDO is a PPS PDO: bits 29:28 select the subtype.
- The port's flags exist only in PDO 1. Bits 29:22 of any other Fixed PDO are reserved,
  and reading them from the active contract's PDO printed Unconstrained Power: No for a
  charger that sets it. Decode through `TPS25751Pdo`, which takes the position.
- Some specification fields are reserved unless another field has a given value. A
  `debugPrint()` that prints them unconditionally reported "Charging" for a battery that
  does not exist. Check each typed debug line against the condition column of its table.

## Patch loader and EEPROM updater

Neither class is a register; do not apply the register criteria to them.

- `bundleSize` in the `PBMs` configuration is 4 bytes, least significant first.
- The burst to 0x30 has no register number and no length byte, and 0x30 there is an I2C
  device address, not the register of the same value.
- The last burst chunk is `min(chunk, remaining)` bytes.
- Each `PatchStartStatus` code maps to its own `PatchLoadStatus` value.
- The `PBMs` configuration is read back and compared before `PBMs` is issued.
- `PBMc` goes through `issueCommand()`, and DATA is read only after the 20 ms wait.
- `PBMe` follows only a burst chunk that is not acknowledged or a rejected `PBMc`, and it
  does not change the returned status.
- An EEPROM region is chosen from the region pointers, not from BOOT_FLAGS alone, and no
  pointer is written before its region has been compared with the image.

## Tests

- Every new register has a suite, a line in the `kAll` table of
  `test_TPS25751RegisterFactory`, its typed reads in `test_TPS25751Typed`, and a row in
  `test_TPS25751RegisterAddress`.
- Each validation tier has a passing and a failing case, and each multi-bit accessor has
  a known-value test.
- A fixture that sets every sub-field to its maximum cannot catch a wrong shift. Each
  multi-field word needs a fixture with distinct values below the maximum.
- A round trip is exact only for inputs that are multiples of the field's LSB.
- Every test function is listed in `RUN_TEST`, and each `TEST_ASSERT_*` can fail.
- Coverage of each new file is above 90% in `test/coverage.sh`.
- A cached build proves nothing about warnings. `platformio.ini` sets a build cache, so
  unchanged objects are retrieved without their warnings. A warning-free claim needs
  `PLATFORMIO_BUILD_CACHE_DIR=<empty scratch dir> pio run -e example-read-registers`
  after `-t clean`, with a `Compiling` line for each new source file in the log.

## Leftovers

- `Serial.print` calls outside `debugPrint()`.
- A validation method that returns `true` as a placeholder.
- A field omitted without a comment saying why.
- A changed value of an existing `Address` enumerator or status enumerator. Additions go
  at the end.

## Downstream device drivers

Review traps for drivers on the I2Cc bus are in `docs/engineering/CONVENTIONS.md` of the
sibling `lib/BQ25798` repository. One originates here: `extractBits16()` is
little-endian, so it byte-swaps a big-endian device's 16-bit register.
