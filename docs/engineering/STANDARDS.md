# TPS25751 Library - Implementation Standards

**Last Updated:** 2025-10-20
**Status:** Active

---

## Overview

This document defines the mandatory implementation standards for all TPS25751 register classes. These standards ensure consistency, maintainability, and quality across the entire library.

**IMPORTANT:** All register implementations MUST follow these standards. Deviations require explicit justification and approval.

---

## Table of Contents

1. [Class Structure](#class-structure)
2. [Factory Integration](#factory-integration)
3. [Validation Requirements](#validation-requirements)
4. [Debug Output Format](#debug-output-format)
5. [Unit Testing Requirements](#unit-testing-requirements)
6. [Memory Management](#memory-management)
7. [Code Documentation](#code-documentation)
8. [Anti-Patterns](#anti-patterns)

---

## Class Structure

> **Note on the current codebase:** the template below shows the intended
> long-term shape (`getAddress()`/`getExpectedSize()`/`isReadOnly()`,
> `validateBasic()`/`validateData()`/`validateSemantic()`). The actual base class
> (`TPS25751Register`) and all implemented registers — including `TPS25751Command`
> and `TPS25751Data` — instead expose `isValid()`, `isValidSize(expectedSize)`,
> `hasValidData()` from the base, and only override the single virtual
> `isSemanticallyValid() const` (chaining the base checks internally) plus
> `debugPrint(Stream& s = Serial) const override`. New register classes should
> match this **real, in-use** pattern, not the template's separate three-method
> spelling, until/unless the base class itself is migrated.

### Mandatory Class Template

Every register class MUST follow this structure:

```cpp
class TPS25751[RegisterName] : public TPS25751Register {
public:
    // Constructors
    TPS25751[RegisterName]();
    TPS25751[RegisterName](const uint8_t* data, size_t length);

    // Rule of Five (implement if managing resources)
    ~TPS25751[RegisterName]() override;
    TPS25751[RegisterName](const TPS25751[RegisterName]& other);
    TPS25751[RegisterName](TPS25751[RegisterName]&& other) noexcept;
    TPS25751[RegisterName]& operator=(const TPS25751[RegisterName]& other);
    TPS25751[RegisterName]& operator=(TPS25751[RegisterName]&& other) noexcept;

    // Register interface - MUST override
    uint8_t getAddress() const override;
    uint8_t getExpectedSize() const override;
    bool isReadOnly() const override;

    // Validation - MUST override all three
    bool validateBasic() const override;
    bool validateData() const override;
    bool validateSemantic() const override;

    // Debug - MUST override
    void debugPrint() const override;

    // Register-specific accessors
    // ... (based on register fields)

private:
    // Helper methods for bitfield extraction
    // Use TPS25751BitUtils for multi-bit fields
};
```

### Constructor Requirements

**Default Constructor:**
```cpp
TPS25751[RegisterName]::TPS25751[RegisterName]()
    : TPS25751Register(TPS25751Registers::Address::[REGISTER_NAME],
                       TPS25751Registers::Registers::[REGISTER_NAME].size) {
    // Initialize with default/zero values
}
```

**Data Constructor:**
```cpp
TPS25751[RegisterName]::TPS25751[RegisterName](const uint8_t* data, size_t length)
    : TPS25751Register(TPS25751Registers::Address::[REGISTER_NAME],
                       TPS25751Registers::Registers::[REGISTER_NAME].size,
                       data, length) {
    // Data is already copied by base class
}
```

### Rule of Five

Implement the Rule of Five if:
- Managing dynamic resources beyond what base class handles
- Need custom copy/move behavior
- Need custom cleanup

If base class handles everything, you can use `= default`:

```cpp
~TPS25751Status() override = default;
TPS25751Status(const TPS25751Status& other) = default;
TPS25751Status(TPS25751Status&& other) noexcept = default;
TPS25751Status& operator=(const TPS25751Status& other) = default;
TPS25751Status& operator=(TPS25751Status&& other) noexcept = default;
```

---

## Factory Integration

Registers are identified solely by their hardware address
(`TPS25751Registers::Address`) — there is no separate type enum. The factory
dispatches object creation directly on the address.

### Step 1: Add the Address (if not already present)

The 27 hardware addresses already live in `TPS25751Registers::Address` in
`TPS25751RegisterAddress.h`, each paired with a `RegisterInfo` size entry. If the
register you are adding is already in that enum (it usually is), there is nothing to
add here.

### Step 2: Add Forward Declaration

At the top of `TPS25751RegisterFactory.h`:

```cpp
class TPS25751[NewRegisterName];
```

### Step 3: Implement Factory Helper Methods

In `TPS25751RegisterFactoryImpl` (both .h and .cpp):

```cpp
// In .h - private section
std::unique_ptr<TPS25751Register> create[NewRegisterName]Register() const;
std::unique_ptr<TPS25751Register> create[NewRegisterName]Register(const uint8_t* data, size_t length) const;

// In .cpp
std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::create[NewRegisterName]Register() const {
    return std::make_unique<TPS25751[NewRegisterName]>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::create[NewRegisterName]Register(
    const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751[NewRegisterName]>(data, length);
}
```

### Step 4: Add a case to each createRegister() switch

Add one `case` keyed on the register's `Address` to each of the three
`createRegister()` variants (no-arg, `(data, length)`, and `(const RegisterInfo&)`):

```cpp
std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(TPS25751Registers::Address addr) {
    switch (addr) {
        // ... existing cases ...
        case TPS25751Registers::Address::[NEW_REGISTER_ADDRESS]:
            return create[NewRegisterName]Register();
        // ...
    }
}
```

Addresses without a `case` fall through to `default:` and return `nullptr`, which is
how un-decoded registers are reported.

**COMMAND (0x08) and DATA (0x09)** now have classes (`TPS25751Command`,
`TPS25751Data`) wired in exactly this way — see them for a worked example of the
four-step checklist above. `TPS25751Data` is a generic 64-byte register with **no
fixed field layout** (its interpretation depends on which 4CC command-task is in
flight; see `TPS25751::executeCommand()` and `TPS25751DownstreamDevice` in
ARCHITECTURE.md), so its semantic validation is intentionally minimal — just the
inherited size/non-null checks (`isValid() && isValidSize(64)`) — rather than
field-specific checks. `TPS25751Command`'s semantic validation accepts the clear
(all-zero) state, the `"!CMD"` rejection code, or any 4-character printable-ASCII
task code.

---

## Validation Requirements

All register classes MUST implement three levels of validation:

### 1. Basic Validation

**Purpose:** Check fundamental data integrity
**When:** Always run first

**Requirements:**
- Verify data length matches expected size
- Check that data pointer is not null (if applicable)
- Verify register address is correct

**Example:**
```cpp
bool TPS25751Status::validateBasic() const {
    // Check data length
    if (getDataLength() != getExpectedSize()) {
        return false;
    }

    // Check address
    if (getAddress() != static_cast<uint8_t>(TPS25751Registers::Address::STATUS)) {
        return false;
    }

    return true;
}
```

### 2. Data Validation

**Purpose:** Verify field-level data validity
**When:** After basic validation passes

**Requirements:**
- Check reserved bits are 0
- Verify enum values are within valid ranges
- Check field constraints (e.g., max values)
- Validate that multi-bit fields don't exceed their bit width

**Example:**
```cpp
bool TPS25751Status::validateData() const {
    if (!validateBasic()) {
        return false;
    }

    // Check reserved bits (example: bits 7-6 of byte 0 must be 0)
    if ((data_[0] & 0xC0) != 0) {
        return false;
    }

    // Check enum validity
    uint8_t connectionState = extractBits(0, 3, 0);
    if (connectionState > 0x07) {  // Max valid value
        return false;
    }

    return true;
}
```

### 3. Semantic Validation

**Purpose:** Verify logical consistency across fields
**When:** After data validation passes

**Requirements:**
- Check inter-field dependencies
- Verify state combinations make sense
- Validate counts match actual data (e.g., PDO count matches PDO array)

**Example:**
```cpp
bool TPS25751ReceivedSourceCaps::validateSemantic() const {
    if (!validateData()) {
        return false;
    }

    // Check PDO count consistency
    uint8_t pdoCount = getPDOCount();
    if (pdoCount > 7) {  // USB PD max
        return false;
    }

    // Verify we have data for all advertised PDOs
    size_t expectedSize = 1 + (pdoCount * 4);  // 1 byte count + 4 bytes per PDO
    if (getDataLength() < expectedSize) {
        return false;
    }

    return true;
}
```

---

## Debug Output Format

All register classes MUST implement `debugPrint()` following this exact format:

### Standard Format

```
========================================
[REGISTER NAME] (0xXX) - XX bytes
========================================
Field Name 1: value (0xHEX)
Field Name 2: value
Field Name 3: descriptive text
  Subfield 1: value
  Subfield 2: value
========================================
```

### Implementation Example

```cpp
void TPS25751Status::debugPrint() const {
    Serial.println(F("========================================"));
    Serial.print(F("STATUS (0x"));
    Serial.print(getAddress(), HEX);
    Serial.print(F(") - "));
    Serial.print(getDataLength());
    Serial.println(F(" bytes"));
    Serial.println(F("========================================"));

    // Print each field with descriptive name
    Serial.print(F("Connection State: "));
    Serial.print(getConnectionStateString());
    Serial.print(F(" (0x"));
    Serial.print(getConnectionState(), HEX);
    Serial.println(F(")"));

    Serial.print(F("VBUS Status: "));
    Serial.println(getVBUSStatus() ? F("Present") : F("Not Present"));

    // ... more fields ...

    Serial.println(F("========================================"));
}
```

### Guidelines

- Use `F()` macro for string literals to save RAM
- Include hex values for numeric fields
- Provide human-readable interpretations
- Indent sub-fields with 2 spaces
- Keep alignment consistent
- Always print field names, even if value is unknown

---

## Unit Testing Requirements

Every register class MUST have comprehensive unit tests with >90% code coverage.

### Required Test Categories

#### 1. Constructor Tests

```cpp
TEST(TPS25751StatusTest, DefaultConstructor) {
    TPS25751Status status;
    EXPECT_EQ(status.getAddress(), 0x1A);
    EXPECT_EQ(status.getExpectedSize(), 5);
    EXPECT_TRUE(status.isReadOnly());
}

TEST(TPS25751StatusTest, DataConstructor) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TPS25751Status status(data, sizeof(data));
    EXPECT_EQ(status.getDataLength(), 5);
    EXPECT_TRUE(status.validateBasic());
}
```

#### 2. Validation Tests

```cpp
TEST(TPS25751StatusTest, ValidateBasic_CorrectLength) {
    uint8_t data[5] = {0};
    TPS25751Status status(data, 5);
    EXPECT_TRUE(status.validateBasic());
}

TEST(TPS25751StatusTest, ValidateBasic_IncorrectLength) {
    uint8_t data[3] = {0};
    TPS25751Status status(data, 3);
    EXPECT_FALSE(status.validateBasic());
}

TEST(TPS25751StatusTest, ValidateData_ReservedBitsZero) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);
    EXPECT_TRUE(status.validateData());
}

TEST(TPS25751StatusTest, ValidateData_ReservedBitsSet) {
    uint8_t data[] = {0xC0, 0x00, 0x00, 0x00, 0x00};  // Reserved bits set
    TPS25751Status status(data, 5);
    EXPECT_FALSE(status.validateData());
}
```

#### 3. Accessor Tests

Test EVERY public accessor method:

```cpp
TEST(TPS25751StatusTest, GetConnectionState) {
    uint8_t data[] = {0x05, 0x00, 0x00, 0x00, 0x00};  // State = 5
    TPS25751Status status(data, 5);
    EXPECT_EQ(status.getConnectionState(), 5);
}

TEST(TPS25751StatusTest, GetConnectionStateString) {
    uint8_t data[] = {0x05, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);
    EXPECT_STREQ(status.getConnectionStateString(), "Connected.UFP");
}
```

#### 4. Edge Case Tests

```cpp
TEST(TPS25751StatusTest, AllBitsSet) {
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TPS25751Status status(data, 5);
    // Verify behavior with all bits set
}

TEST(TPS25751StatusTest, AllBitsZero) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);
    // Verify behavior with all bits zero
}

TEST(TPS25751StatusTest, BoundaryValues) {
    // Test max valid values for each field
}
```

#### 5. Factory Creation Tests

```cpp
TEST(TPS25751StatusTest, FactoryCreation) {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::STATUS);
    EXPECT_NE(reg, nullptr);

    // Cast and verify type
    auto* status = static_cast<TPS25751Status*>(reg.get());
    EXPECT_NE(status, nullptr);
    EXPECT_EQ(status->getAddress(), 0x1A);
}

TEST(TPS25751StatusTest, FactoryCreationWithData) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto reg = TPS25751Factory::getInstance().createRegister(
        TPS25751Registers::Address::STATUS, data, sizeof(data));
    EXPECT_NE(reg, nullptr);
    EXPECT_EQ(reg->getDataLength(), 5);
}
```

### Test Organization

```
test/
  test_TPS25751Status/
    test_TPS25751Status.cpp      # Main test file
  test_TPS25751Mode/
    test_TPS25751Mode.cpp
  ...
```

---

## Memory Management

### RAII Principles

**Always follow RAII (Resource Acquisition Is Initialization):**

✅ **DO:**
```cpp
class TPS25751Status : public TPS25751Register {
public:
    TPS25751Status(const uint8_t* data, size_t length)
        : TPS25751Register(address, size, data, length) {
        // Base class handles data copying
        // No manual memory management needed
    }
};
```

❌ **DON'T:**
```cpp
class TPS25751Status : public TPS25751Register {
private:
    uint8_t* manualBuffer_;  // Manual memory management
public:
    TPS25751Status() {
        manualBuffer_ = new uint8_t[5];  // Manual allocation - avoid!
    }
};
```

### Smart Pointers

**Use `std::unique_ptr` for ownership:**

✅ **DO:**
```cpp
std::unique_ptr<TPS25751Register> reg =
    TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::STATUS);
```

**Use raw pointers ONLY for non-owning references:**

```cpp
void processRegister(const TPS25751Register* reg) {  // Non-owning
    // Use reg, but don't delete it
}
```

### Move Semantics

**Prefer moving over copying for efficiency:**

```cpp
// Move constructor
TPS25751Status(TPS25751Status&& other) noexcept
    : TPS25751Register(std::move(other)) {
    // Move any additional members
}

// Move assignment
TPS25751Status& operator=(TPS25751Status&& other) noexcept {
    if (this != &other) {
        TPS25751Register::operator=(std::move(other));
        // Move any additional members
    }
    return *this;
}
```

### Platform Constraint: No RTTI

**CRITICAL:** Platform compiles with `-fno-rtti`

❌ **NEVER use:**
```cpp
auto* status = dynamic_cast<TPS25751Status*>(reg.get());  // FAILS!
if (typeid(*reg) == typeid(TPS25751Status)) { }          // FAILS!
```

✅ **ALWAYS use:**
```cpp
auto* status = static_cast<TPS25751Status*>(reg.get());  // OK
```

**Safe casting pattern:**
```cpp
// Dispatch on the register's address (the single register identity)
if (reg->getAddress() == static_cast<uint8_t>(TPS25751Registers::Address::STATUS)) {
    auto* status = static_cast<TPS25751Status*>(reg.get());
    // Now safe to use status
}
```

---

## Code Documentation

### Doxygen Comments Required

**Every public method MUST have Doxygen documentation:**

```cpp
/**
 * @brief Get the Type-C connection state
 *
 * The connection state indicates the current state of the Type-C state machine,
 * including whether the device is connected, the role (UFP/DFP), and debug
 * accessory mode status.
 *
 * @return uint8_t Connection state value (0-7)
 *
 * @see TI TPS25751 Technical Reference Manual, Section 8.6.2.2, Table 8-35
 *
 * Possible values:
 * - 0x00: Not connected
 * - 0x01: Connected.DFP
 * - 0x02: Connected.UFP
 * - 0x03: Connected.Debug Accessory (DFP)
 * - 0x04: Connected.Debug Accessory (UFP)
 * - 0x05: Audio Accessory
 * - 0x06-0x07: Reserved
 */
uint8_t getConnectionState() const;
```

### Class Documentation

```cpp
/**
 * @file TPS25751Status.h
 * @brief STATUS register (0x1A) class implementation
 *
 * The STATUS register provides real-time information about the Type-C connection
 * state, VBUS status, power role, data role, and plug orientation.
 *
 * Register Details:
 * - Address: 0x1A
 * - Size: 5 bytes
 * - Access: Read-only
 *
 * @see TI TPS25751 Technical Reference Manual, Section 8.6.2.2
 */
class TPS25751Status : public TPS25751Register {
    // ...
};
```

### Reference Documentation

**Always reference:**
- Datasheet section numbers
- Bit field names from datasheet
- USB PD specification sections (for PD-related registers)

---

## Anti-Patterns

### ❌ Things You Must NEVER Do

#### 1. No Dynamic Cast
```cpp
// WRONG - will not compile
auto* status = dynamic_cast<TPS25751Status*>(reg.get());
```

#### 2. No Type ID
```cpp
// WRONG - will not compile
if (typeid(*reg) == typeid(TPS25751Status)) { }
```

#### 3. No static_assert(false) in Templates
```cpp
// WRONG - causes compilation errors
template<typename T>
void foo() {
    static_assert(false, "Not implemented");  // Fails immediately!
}
```

Use SFINAE instead:
```cpp
template<typename T, typename = void>
struct is_register : std::false_type {};

template<typename T>
struct is_register<T, std::void_t<decltype(T::getAddress())>> : std::true_type {};
```

#### 4. No Manual Memory Management
```cpp
// WRONG
uint8_t* buffer = new uint8_t[64];
// ... use buffer ...
delete[] buffer;  // Easy to forget or leak!
```

#### 5. No Magic Numbers
```cpp
// WRONG
uint8_t state = data_[0] & 0x07;

// RIGHT
static constexpr uint8_t CONNECTION_STATE_MASK = 0x07;
uint8_t state = data_[0] & CONNECTION_STATE_MASK;
```

#### 6. No String Duplication in Flash
```cpp
// WRONG - wastes flash memory
Serial.println("Connection State: Not Connected");
// ... later ...
Serial.println("Connection State: Not Connected");  // Duplicate!

// RIGHT - use helper functions or lookup tables
const char* getConnectionStateString() const;
```

---

## Checklist for New Register Implementation

Use this checklist for every new register:

- [ ] Class inherits from `TPS25751Register`
- [ ] Default and data constructors implemented
- [ ] Rule of Five implemented (or defaulted)
- [ ] `getAddress()` returns correct register address
- [ ] `getExpectedSize()` returns correct size
- [ ] `isReadOnly()` returns correct access type
- [ ] `validateBasic()` checks length and address
- [ ] `validateData()` checks reserved bits and field ranges
- [ ] `validateSemantic()` checks logical consistency
- [ ] `debugPrint()` follows standard format
- [ ] All accessor methods have Doxygen comments
- [ ] Register address present in `TPS25751Registers::Address` (+ `RegisterInfo` size entry)
- [ ] Forward declaration added to factory header
- [ ] Factory creation methods implemented
- [ ] All `createRegister()` variants handle the new address (`case` added to each switch)
- [ ] Unit tests written with >90% coverage
- [ ] Constructor tests pass
- [ ] Validation tests pass
- [ ] Accessor tests pass
- [ ] Edge case tests pass
- [ ] Factory creation tests pass
- [ ] No compiler warnings
- [ ] Compiles successfully on Teensy platform
- [ ] Hardware tested (if hardware available)

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial standards document extracted from implementation plan |

---

*These standards are mandatory. Adherence ensures library quality and maintainability.*
