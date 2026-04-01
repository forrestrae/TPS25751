# Code Review Guidelines

This document outlines guidelines for reviewing TPS25751 library code changes, with emphasis on embedded C++ concerns specific to the Teensy 4.x platform and TPS25751 I2C protocol.

---

## Platform-Specific Correctness

The most critical review category for this codebase. Many bugs here cause silent runtime failures or non-compiling builds.

- **RTTI usage**: Any `dynamic_cast<>` or `typeid()` call will **not compile** (`-fno-rtti`). Use `static_cast<>` with factory-based type verification
- **`static_assert(false)` in templates**: Causes compilation errors even for unused template specializations. Use SFINAE/dependent types instead
- **Missing `F()` macro**: String literals passed to `Serial.print/println` without `F()` waste RAM. All string literals in debug output must use `F()`
- **I2C length byte**: The TPS25751 protocol prefixes responses with a byte count. Forgetting to skip byte 0 when parsing register data is a common bug
- **PDO unit mismatch**: Fixed-supply and PPS (Programmable Power Supply) PDOs use different voltage/current units. Always verify the conversion factor matches the PDO type
- **Stack usage**: Deep call stacks and large stack-allocated arrays can overflow the limited RAM. Prefer `std::unique_ptr` and heap allocation for buffers > ~64 bytes

---

## Hard-to-Notice Bugs

- **Off-by-one in bitfield extraction**: Verify bit positions match the TI datasheet exactly — bit 0 vs bit 1 shifts are easy to transpose
- **Byte order in multi-byte fields**: Confirm endianness matches TPS25751 register layout; little-endian fields in a big-endian context (or vice versa) will produce wrong values silently
- **Reserved bit masking**: Reserved bits must be masked out before comparing or validating field values; raw reads may have non-zero reserved bits set by hardware
- **Integer overflow in bitfield math**: Shifting or multiplying `uint8_t` values before casting to a wider type can overflow; cast first
- **Null pointer on factory miss**: `createRegister()` returns `nullptr` for unknown `RegisterType` values. Callers must check before dereferencing
- **Validation tier skipping**: `validateData()` must call `validateBasic()` first; `validateSemantic()` must call `validateData()` first. Breaking this chain produces misleading results
- **Move-after-use**: After moving a `std::unique_ptr` into a factory return, the source is null. Ensure moved-from objects are not accessed
- **Incomplete Rule of Five**: If any of copy constructor, copy assignment, move constructor, move assignment, or destructor is user-defined, all five must be considered — missing one leads to double-free or shallow copies
- **Signed/unsigned comparison warnings**: Comparing `int` loop indices against `size_t` lengths can wrap; use consistent types
- **PDO count vs data length mismatch**: In capability registers, verify that `getPDOCount()` * bytes-per-PDO + header never exceeds the actual data buffer size

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
- **Factory integration**: New register type added to `RegisterType` enum, forward declaration added, all `createRegister()` variants updated, `getRegisterType()` and `getAddress()` helpers updated
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

---

## Additional Concerns

- **Performance**: Unnecessary copies of register data (pass `const TPS25751Register*` or `const TPS25751Register&` instead of by value)
- **Backwards compatibility**: Changes to `RegisterType` enum values (not just additions) break binary compatibility with serialized data
- **Missing datasheet reference**: Any bitfield constant or enum value without a `@see` TI TRM section reference makes future verification impossible
- **Hardware test status**: Note in the PR whether changes have been validated on physical TPS25751 hardware