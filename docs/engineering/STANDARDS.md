# TPS25751 library: implementation standards

The code is the reference. This document names the classes to copy and states the rules
that the code alone does not make obvious. It carries no class templates, because a
template in a document drifts from the base class it describes.

## Reference classes

Copy the closest existing class instead of starting from a blank file.

| To write | Copy | Why this one |
|---|---|---|
| A read-only register | `TPS25751Version`, `TPS25751ADCResults` | Accessors only, raw and unit-converted forms |
| A read/write register | `TPS25751SleepControl`, `TPS25751AutoNegotiateSink` | `setBits()` setters, `kAddress`, a cross-field semantic rule |
| A register with fields past bit 255 | `TPS25751TransmitBatteryCapabilities` | Bit positions computed in `uint16_t` |
| A register that carries PDOs or RDOs | `TPS25751TransmitSourceCaps` | Hands out `TPS25751Pdo` values (ADR-013) |
| A register test suite | `test/test_native/test_TPS25751SleepControl` | Every required case for a read/write register |

## Register class rules

- Inherit `TPS25751Register` directly. The base owns the byte buffer and implements the
  copy and move operations, so a register class declares no destructor and no copy or
  move members, and adds no member that owns memory.
- Provide the four constructors the reference classes have: default (a zeroed buffer of
  the register's `RegisterInfo` size), `(const uint8_t*)`, `(const uint8_t*, size_t)`,
  and `(RegisterInfo)`.
- Read every field through `extractBits*()` or `extractEnum*()`, and write every field
  through `setBits()`. Do not index `_raw[]` or restate a field mask. Keep a computed bit
  position in `uint16_t` or wider (ADR-012).
- Give each field a raw accessor. Add a unit accessor (`*_mV()`, `*Millivolts()`, and so
  on) when the TRM gives a scale, and an `enum class` with a free `toString()` when it
  gives named values.
- Cite the TRM in the Doxygen comment of the class and of each accessor: document
  number, revision, table, page, and bit range. For a field the TRM defers to USB PD,
  cite the specification table as well.

## Read/write registers

- Declare `static constexpr TPS25751Registers::Address kAddress`. `writeRegister<T>()`
  takes the address from the class, so a class can be written only to its own register.
- Write one setter per accessor, named `set<Accessor>`. A setter is one `setBits()` call,
  which guards an invalid buffer, preserves the neighbouring bits, and masks the value to
  the field width. A unit setter rounds down and clamps to the field maximum.
- Add a `write<Name>()` method to `TPS25751` that forwards to `writeRegister<T>()`.
- A register with no runtime write path (`TPS25751IOConfig`, `TPS25751CustomerUse`) has
  no setters and no `kAddress`.

## Wiring a new register in

A register is identified by its `TPS25751Registers::Address`; there is no separate type
enum. A new class touches these places, and a missed one fails silently by returning
`nullptr`:

1. `include/TPS25751RegisterAddress.h`: the `Address` enumerator, the `Registers::`
   `RegisterInfo` constant, a case in `getRegisterSize()`, and a case in
   `getRegisterInfo()`. The size is the TRM width rounded up to whole bytes unless
   hardware reports a different length byte (CONSTRAINTS.md, Read/Write Registers).
2. `include/TPS25751RegisterFactory.h`: a forward declaration and the two private
   `create<Name>Register()` declarations.
3. `src/TPS25751RegisterFactory.cpp`: the `#include`, the two creators, and a case in
   each of the three `createRegister()` switches.
4. `include/TPS25751.h` and `src/TPS25751.cpp`: the `#include`, `read<Name>(T&)`,
   `read<Name>Register(bool validate)`, and for a read/write register `write<Name>()`.
5. `platformio.ini`: the new `.cpp` in `build_src_filter` of `[env:native]`, which lists
   its sources one by one.
6. `test/test_native/test_TPS25751<Name>/`: the suite (TESTING.md, Required Test Cases).
7. `test/test_native/test_TPS25751RegisterFactory` and `test_TPS25751Typed`: the new
   address in each.

## Validation

The base class implements the first two tiers, `isValid()` and `hasValidData()`. A
register overrides only `isSemanticallyValid()`, which starts with the base checks and
the size check, then applies the field rules:

- Reserved bits read 0, but only after hardware confirms it. The firmware sets
  TRM-reserved bits in some registers, and a reserved-bit check then rejects every valid
  read (CONSTRAINTS.md, Read/Write Registers).
- Enum fields hold a defined value.
- Cross-field rules the TRM or USB PD R3.3 states, such as low threshold at most high
  threshold.

The documented reset value of a register must pass. Do not add a rule the TRM does not
state; record an inferred rule as inferred in the header comment.

## Debug output

`debugPrint(Stream &s = Serial) const` prints to the stream it is given, never to
`Serial` directly. Follow `TPS25751SleepControl::debugPrint()`:

- A banner of 40 `=`, then `NAME (0xNN) - N bytes`, then the banner again, and a closing
  banner.
- One line per field: the name, the decoded text, and the raw value in hex in
  parentheses.
- Every string literal in `F()`.
- Fixed-width hex through `TPS25751Register::printHex()`, because `Stream`'s `HEX` drops
  leading zeros. Booleans through `yesNo()`.
- A USB PD encoding that more than one register carries decodes through the shared
  definition in `TPS25751UsbPd.h`.

Older classes such as `TPS25751Status` use a shorter `=== Name (0xNN) ===` header. Do not
copy it into new classes.

## Platform rules

CONSTRAINTS.md explains each of these.

- No RTTI: use `static_cast` on an object whose address you chose, or
  `readRegisterAs<T>()`. `dynamic_cast` and `typeid` do not compile.
- No `static_assert(false, ...)` in a template body.
- No `new` or `delete` outside the register base class. Return ownership in a
  `std::unique_ptr`.
- No large buffers on the stack. The largest in the library is the 64-byte DATA image.

## Doxygen

Every public class, method, and enumerator has a Doxygen comment. A comment states what
the TRM says, the unit, the reset value where it matters, and anything hardware showed
that the TRM does not say. It does not restate the method name. Mark a renamed or
superseded name `[[deprecated]]` with the replacement and keep it for one major version.

The repository has no Doxyfile and does not publish generated API pages; the headers are
the API reference.

## Downstream device drivers

Drivers for parts on the I2Cc bus live in their own repositories and follow the
conventions in `docs/engineering/CONVENTIONS.md` of the sibling `lib/BQ25798` repository,
the reference implementation (ADR-008, ADR-009). The one rule that originates here:
`extractBits16()` is little-endian, so a big-endian device assembles its multi-byte
fields by hand.

## Checklist for a new register

- [ ] Field layout taken from the `tps25751-docs` MCP server, and from `usb-pd-docs` for
      a USB PD object
- [ ] Class copied from the closest reference class
- [ ] Every place in "Wiring a new register in" updated
- [ ] `isSemanticallyValid()` accepts the documented reset value
- [ ] Native suite covers every accessor and setter, each validation tier, and
      `debugPrint()`; `test/coverage.sh` shows the file above 90%
- [ ] `pio test -e native` and `pio run -e example-read-registers` pass
- [ ] Read on hardware where a board is available, and any difference from the TRM
      recorded in the header and in CONSTRAINTS.md
