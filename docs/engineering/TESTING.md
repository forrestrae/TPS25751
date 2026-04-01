# TPS25751 Library - Testing Strategy

**Last Updated:** 2025-10-20
**Status:** Active

---

## Overview

This document defines the comprehensive testing strategy for the TPS25751 library. Our testing approach follows a multi-layered strategy: unit tests, integration tests, and hardware validation.

**Quality Target:** >90% code coverage for all register implementations.

---

## Table of Contents

1. [Testing Layers](#testing-layers)
2. [Unit Testing](#unit-testing)
3. [Integration Testing](#integration-testing)
4. [Hardware Testing](#hardware-testing)
5. [Test Organization](#test-organization)
6. [Running Tests](#running-tests)
7. [Test Data Management](#test-data-management)
8. [Continuous Testing](#continuous-testing)

---

## Testing Layers

### 1. Unit Testing (Isolation)

**Purpose:** Test individual register classes in complete isolation
**Framework:** PlatformIO native testing framework
**Scope:** Each register class independently

**Coverage:**
- Constructor behavior
- All validation levels (basic, data, semantic)
- All accessor methods
- Edge cases and boundary conditions
- Factory creation

**Dependencies:** None (or mocked)

### 2. Integration Testing (Interaction)

**Purpose:** Test interactions between components
**Framework:** PlatformIO native testing framework
**Scope:** Multiple components working together

**Coverage:**
- Factory creation for all register types
- Round-trip read/write for R/W registers
- Interaction with main TPS25751 controller class
- Multiple register reads in sequence
- Error handling across component boundaries

**Dependencies:** Real implementations, mocked I2C

### 3. Hardware Testing (Validation)

**Purpose:** Validate against actual TPS25751 hardware
**Framework:** Manual testing with example sketches
**Scope:** Real-world behavior verification

**Coverage:**
- Actual I2C communication
- Real register values from hardware
- Various USB-C devices and chargers
- Power negotiation sequences
- Edge cases with real devices

**Dependencies:** Physical hardware, USB-C test devices

---

## Unit Testing

### Test Structure

Each register class has its own test file:

```
test/
  test_TPS25751Status/
    test_TPS25751Status.cpp
  test_TPS25751Mode/
    test_TPS25751Mode.cpp
  test_TPS25751PowerPathStatus/
    test_TPS25751PowerPathStatus.cpp
  ...
```

### Required Test Cases

#### 1. Constructor Tests

**Test default constructor initialization:**

```cpp
#include <unity.h>
#include "TPS25751Status.h"

void test_default_constructor() {
    TPS25751Status status;

    TEST_ASSERT_EQUAL_UINT8(0x1A, status.getAddress());
    TEST_ASSERT_EQUAL_UINT8(5, status.getExpectedSize());
    TEST_ASSERT_TRUE(status.isReadOnly());
    TEST_ASSERT_EQUAL_UINT8(0, status.getDataLength());
}

void test_data_constructor_valid() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TPS25751Status status(data, sizeof(data));

    TEST_ASSERT_EQUAL_UINT8(5, status.getDataLength());
    TEST_ASSERT_TRUE(status.validateBasic());
}

void test_data_constructor_invalid_length() {
    uint8_t data[] = {0x01, 0x02, 0x03};  // Wrong length
    TPS25751Status status(data, sizeof(data));

    TEST_ASSERT_EQUAL_UINT8(3, status.getDataLength());
    TEST_ASSERT_FALSE(status.validateBasic());  // Should fail
}
```

#### 2. Validation Tests

**Test all three validation levels:**

```cpp
void test_validate_basic_correct_length() {
    uint8_t data[5] = {0};
    TPS25751Status status(data, 5);
    TEST_ASSERT_TRUE(status.validateBasic());
}

void test_validate_basic_incorrect_length() {
    uint8_t data[3] = {0};
    TPS25751Status status(data, 3);
    TEST_ASSERT_FALSE(status.validateBasic());
}

void test_validate_data_reserved_bits_zero() {
    // Data with all reserved bits zero
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);
    TEST_ASSERT_TRUE(status.validateData());
}

void test_validate_data_reserved_bits_set() {
    // Data with reserved bits set (invalid)
    uint8_t data[] = {0xC0, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);
    TEST_ASSERT_FALSE(status.validateData());
}

void test_validate_semantic_logical_consistency() {
    // For registers with inter-field dependencies
    // Example: PDO count matches actual PDOs
    uint8_t data[] = {
        0x03,  // PDO count = 3
        // ... 3 PDOs worth of data ...
    };
    TPS25751ReceivedSourceCaps caps(data, sizeof(data));
    TEST_ASSERT_TRUE(caps.validateSemantic());
}

void test_validate_semantic_inconsistent() {
    uint8_t data[] = {
        0x05,  // PDO count = 5, but only 3 PDOs of data
        // ... 3 PDOs worth of data ...
    };
    TPS25751ReceivedSourceCaps caps(data, sizeof(data));
    TEST_ASSERT_FALSE(caps.validateSemantic());
}
```

#### 3. Accessor Tests

**Test every public accessor method:**

```cpp
void test_get_connection_state() {
    uint8_t data[] = {0x05, 0x00, 0x00, 0x00, 0x00};  // State = 5
    TPS25751Status status(data, 5);

    TEST_ASSERT_EQUAL_UINT8(5, status.getConnectionState());
}

void test_get_connection_state_all_values() {
    for (uint8_t state = 0; state <= 7; state++) {
        uint8_t data[] = {state, 0x00, 0x00, 0x00, 0x00};
        TPS25751Status status(data, 5);

        TEST_ASSERT_EQUAL_UINT8(state, status.getConnectionState());
    }
}

void test_get_connection_state_string() {
    struct TestCase {
        uint8_t state;
        const char* expected;
    };

    TestCase cases[] = {
        {0, "Not Connected"},
        {1, "Connected.DFP"},
        {2, "Connected.UFP"},
        // ... all valid states
    };

    for (const auto& tc : cases) {
        uint8_t data[] = {tc.state, 0x00, 0x00, 0x00, 0x00};
        TPS25751Status status(data, 5);

        TEST_ASSERT_EQUAL_STRING(tc.expected, status.getConnectionStateString());
    }
}

void test_vbus_present() {
    // VBUS bit set
    uint8_t data1[] = {0x00, 0x01, 0x00, 0x00, 0x00};
    TPS25751Status status1(data1, 5);
    TEST_ASSERT_TRUE(status1.isVBUSPresent());

    // VBUS bit clear
    uint8_t data2[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status2(data2, 5);
    TEST_ASSERT_FALSE(status2.isVBUSPresent());
}
```

#### 4. Edge Case Tests

**Test boundary conditions and extreme values:**

```cpp
void test_all_bits_zero() {
    uint8_t data[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);

    // Verify all getters return expected zero/default values
    TEST_ASSERT_EQUAL_UINT8(0, status.getConnectionState());
    TEST_ASSERT_FALSE(status.isVBUSPresent());
    // ... test all accessors
}

void test_all_bits_set() {
    uint8_t data[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TPS25751Status status(data, 5);

    // Verify behavior with all bits set
    // Some fields may have max values, some may be invalid
    // Document expected behavior
}

void test_boundary_values() {
    // Test maximum valid values for each field
    uint8_t data[] = {
        0x07,  // Max connection state
        0xFF,  // All flags set
        // ... boundary values for each field
    };
    TPS25751Status status(data, 5);

    TEST_ASSERT_EQUAL_UINT8(7, status.getConnectionState());
}

void test_empty_data() {
    TPS25751Status status(nullptr, 0);

    TEST_ASSERT_EQUAL_UINT8(0, status.getDataLength());
    TEST_ASSERT_FALSE(status.validateBasic());
}

void test_oversized_data() {
    uint8_t data[10] = {0};  // Larger than expected
    TPS25751Status status(data, 10);

    TEST_ASSERT_EQUAL_UINT8(10, status.getDataLength());
    TEST_ASSERT_FALSE(status.validateBasic());  // Length mismatch
}
```

#### 5. Factory Creation Tests

**Test factory pattern integration:**

```cpp
void test_factory_create_by_type() {
    auto reg = TPS25751Factory::getInstance().createRegister(RegisterType::STATUS);

    TEST_ASSERT_NOT_NULL(reg.get());
    TEST_ASSERT_EQUAL_UINT8(0x1A, reg->getAddress());
    TEST_ASSERT_EQUAL_UINT8(5, reg->getExpectedSize());
}

void test_factory_create_by_address() {
    auto reg = TPS25751Factory::getInstance().createRegister(
        TPS25751Registers::Address::STATUS);

    TEST_ASSERT_NOT_NULL(reg.get());
    TEST_ASSERT_EQUAL_UINT8(0x1A, reg->getAddress());
}

void test_factory_create_with_data() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto reg = TPS25751Factory::getInstance().createRegister(
        RegisterType::STATUS, data, sizeof(data));

    TEST_ASSERT_NOT_NULL(reg.get());
    TEST_ASSERT_EQUAL_UINT8(5, reg->getDataLength());
}

void test_factory_type_address_mapping() {
    RegisterType type = TPS25751RegisterFactory::getRegisterType(
        TPS25751Registers::Address::STATUS);

    TEST_ASSERT_EQUAL(RegisterType::STATUS, type);

    TPS25751Registers::Address addr =
        TPS25751RegisterFactory::getAddress(RegisterType::STATUS);

    TEST_ASSERT_EQUAL_UINT8(0x1A, static_cast<uint8_t>(addr));
}
```

#### 6. Copy and Move Tests

**Test Rule of Five implementation:**

```cpp
void test_copy_constructor() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TPS25751Status original(data, 5);

    TPS25751Status copy(original);

    TEST_ASSERT_EQUAL_UINT8(original.getAddress(), copy.getAddress());
    TEST_ASSERT_EQUAL_UINT8(original.getDataLength(), copy.getDataLength());
    TEST_ASSERT_EQUAL_UINT8(original.getConnectionState(), copy.getConnectionState());
}

void test_copy_assignment() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TPS25751Status original(data, 5);
    TPS25751Status copy;

    copy = original;

    TEST_ASSERT_EQUAL_UINT8(original.getConnectionState(), copy.getConnectionState());
}

void test_move_constructor() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TPS25751Status original(data, 5);
    uint8_t expectedState = original.getConnectionState();

    TPS25751Status moved(std::move(original));

    TEST_ASSERT_EQUAL_UINT8(expectedState, moved.getConnectionState());
}

void test_move_assignment() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TPS25751Status original(data, 5);
    uint8_t expectedState = original.getConnectionState();
    TPS25751Status moved;

    moved = std::move(original);

    TEST_ASSERT_EQUAL_UINT8(expectedState, moved.getConnectionState());
}
```

### Test Template

Use this template for new register tests:

```cpp
#include <unity.h>
#include "TPS25751[RegisterName].h"
#include "TPS25751RegisterFactory.h"

// Test data constants
namespace {
    constexpr uint8_t VALID_DATA[] = { /* valid test data */ };
    constexpr uint8_t INVALID_DATA[] = { /* invalid test data */ };
}

// Constructor tests
void test_default_constructor() { /* ... */ }
void test_data_constructor_valid() { /* ... */ }
void test_data_constructor_invalid() { /* ... */ }

// Validation tests
void test_validate_basic_correct() { /* ... */ }
void test_validate_basic_incorrect() { /* ... */ }
void test_validate_data_valid() { /* ... */ }
void test_validate_data_invalid() { /* ... */ }
void test_validate_semantic_consistent() { /* ... */ }
void test_validate_semantic_inconsistent() { /* ... */ }

// Accessor tests (one per public method)
void test_get_field1() { /* ... */ }
void test_get_field2() { /* ... */ }
// ...

// Edge case tests
void test_all_bits_zero() { /* ... */ }
void test_all_bits_set() { /* ... */ }
void test_boundary_values() { /* ... */ }

// Factory tests
void test_factory_creation() { /* ... */ }

// Rule of Five tests
void test_copy_constructor() { /* ... */ }
void test_move_constructor() { /* ... */ }

// Setup and runner
void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Constructor tests
    RUN_TEST(test_default_constructor);
    RUN_TEST(test_data_constructor_valid);
    RUN_TEST(test_data_constructor_invalid);

    // Validation tests
    RUN_TEST(test_validate_basic_correct);
    RUN_TEST(test_validate_basic_incorrect);
    RUN_TEST(test_validate_data_valid);
    RUN_TEST(test_validate_data_invalid);
    RUN_TEST(test_validate_semantic_consistent);
    RUN_TEST(test_validate_semantic_inconsistent);

    // Accessor tests
    RUN_TEST(test_get_field1);
    RUN_TEST(test_get_field2);

    // Edge case tests
    RUN_TEST(test_all_bits_zero);
    RUN_TEST(test_all_bits_set);
    RUN_TEST(test_boundary_values);

    // Factory tests
    RUN_TEST(test_factory_creation);

    // Rule of Five tests
    RUN_TEST(test_copy_constructor);
    RUN_TEST(test_move_constructor);

    return UNITY_END();
}
```

---

## Integration Testing

### I2C Mock Testing

Create mock I2C layer for testing without hardware:

```cpp
class MockI2C {
public:
    void setNextReadData(const uint8_t* data, size_t length) {
        readData_.assign(data, data + length);
        readIndex_ = 0;
    }

    uint8_t read() {
        if (readIndex_ < readData_.size()) {
            return readData_[readIndex_++];
        }
        return 0;
    }

    void write(uint8_t data) {
        writeData_.push_back(data);
    }

    const std::vector<uint8_t>& getWrittenData() const {
        return writeData_;
    }

private:
    std::vector<uint8_t> readData_;
    std::vector<uint8_t> writeData_;
    size_t readIndex_ = 0;
};
```

### Factory Integration Tests

```cpp
void test_factory_creates_all_register_types() {
    RegisterType types[] = {
        RegisterType::STATUS,
        RegisterType::MODE,
        RegisterType::BOOT_FLAGS,
        RegisterType::POWER_PATH_STATUS,
        RegisterType::PORT_CONFIG,
        RegisterType::TYPEC_STATE,
        RegisterType::INTERRUPT_EVENT,
        RegisterType::POWER_STATUS,
        RegisterType::PD_STATUS,
        // ... add all implemented types
    };

    for (auto type : types) {
        auto reg = TPS25751Factory::getInstance().createRegister(type);
        TEST_ASSERT_NOT_NULL(reg.get());
    }
}

void test_all_addresses_map_to_types() {
    TPS25751Registers::Address addresses[] = {
        TPS25751Registers::Address::STATUS,
        TPS25751Registers::Address::MODE,
        // ... all implemented addresses
    };

    for (auto addr : addresses) {
        RegisterType type = TPS25751RegisterFactory::getRegisterType(addr);
        TEST_ASSERT_NOT_EQUAL(RegisterType::UNKNOWN, type);

        // Verify round-trip
        TPS25751Registers::Address mappedAddr =
            TPS25751RegisterFactory::getAddress(type);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(addr),
            static_cast<uint8_t>(mappedAddr)
        );
    }
}
```

### Multi-Register Sequence Tests

```cpp
void test_read_multiple_registers_sequence() {
    // Mock I2C controller
    MockI2C mockI2C;

    // Set up mock data for multiple register reads
    uint8_t statusData[] = {0x05, 0x02, 0x00, 0x01, 0x00, 0x00};  // 5 bytes + length prefix
    uint8_t modeData[] = {0x04, 0x01, 0x02, 0x03, 0x04};         // 4 bytes + length prefix

    // Simulate reading STATUS then MODE
    mockI2C.setNextReadData(statusData, sizeof(statusData));
    auto status = readStatusRegister(mockI2C);
    TEST_ASSERT_TRUE(status->validateData());

    mockI2C.setNextReadData(modeData, sizeof(modeData));
    auto mode = readModeRegister(mockI2C);
    TEST_ASSERT_TRUE(mode->validateData());
}
```

### Round-Trip R/W Testing

```cpp
void test_port_control_round_trip() {
    MockI2C mockI2C;

    // Create register with specific values
    TPS25751PortControl control;
    control.setPortEnabled(true);
    control.setDataRoleSwapEnabled(true);

    // Write to mock I2C
    control.writeToI2C(mockI2C);

    // Verify written data
    auto writtenData = mockI2C.getWrittenData();
    TEST_ASSERT_GREATER_THAN(0, writtenData.size());

    // Read back
    mockI2C.setNextReadData(writtenData.data(), writtenData.size());
    TPS25751PortControl readBack;
    readBack.readFromI2C(mockI2C);

    // Verify values match
    TEST_ASSERT_EQUAL(control.isPortEnabled(), readBack.isPortEnabled());
    TEST_ASSERT_EQUAL(control.isDataRoleSwapEnabled(), readBack.isDataRoleSwapEnabled());
}
```

---

## Hardware Testing

### Hardware Test Setup

**Required Hardware:**
- Teensy 4.0 or 4.1 development board
- TPS25751 evaluation board or device
- Various USB-C cables and devices for testing:
  - USB-C chargers (5V, 9V, 12V, 15V, 20V capable)
  - USB-C power banks
  - USB-C devices (phones, laptops, etc.)
  - USB-C to USB-A adapters
  - USB-C debug accessory (if available)

### Test Sketches

Create example sketches for hardware validation:

```cpp
// examples/register_dump.ino
// Dumps all register values from connected TPS25751

#include <TPS25751.h>

TPS25751 tps;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println("TPS25751 Register Dump");
    Serial.println("======================");

    if (!tps.begin()) {
        Serial.println("ERROR: Failed to initialize TPS25751");
        return;
    }
}

void loop() {
    // Read and print all registers
    auto status = tps.readStatusRegister();
    if (status) {
        status->debugPrint();
    }

    auto mode = tps.readModeRegister();
    if (mode) {
        mode->debugPrint();
    }

    // ... read all other registers ...

    delay(1000);
}
```

### Hardware Validation Checklist

For each register implementation:

- [ ] Register reads successfully from hardware
- [ ] Parsed values match expected behavior
- [ ] Changes in hardware state reflected in register
- [ ] Write operations (if applicable) modify hardware correctly
- [ ] Validation passes with real hardware data
- [ ] No I2C communication errors
- [ ] Values match datasheet descriptions
- [ ] Edge cases tested (connect/disconnect, power changes, etc.)

### Power Delivery Test Scenarios

**Test various PD negotiation scenarios:**

1. **5V Fixed Supply**
   - Connect 5V USB-C charger
   - Verify ACTIVE_PDO shows 5V contract
   - Verify VBUS present

2. **Multi-Voltage Charger**
   - Connect PD charger with multiple voltages (5V, 9V, 12V, 15V, 20V)
   - Verify RECEIVED_SOURCE_CAPABILITIES shows all PDOs
   - Verify correct negotiation to expected voltage

3. **Role Swap**
   - Test DFP to UFP transition
   - Verify STATUS register reflects role changes
   - Check TYPEC_STATE transitions

4. **Debug Accessory Mode**
   - Connect debug accessory
   - Verify connection state reflects debug mode
   - Check orientation detection

5. **Disconnection**
   - Hot plug/unplug scenarios
   - Verify clean state transitions
   - Check interrupt events

---

## Test Organization

### Directory Structure

```
test/
  ├── test_TPS25751Status/
  │   └── test_TPS25751Status.cpp
  ├── test_TPS25751Mode/
  │   └── test_TPS25751Mode.cpp
  ├── test_TPS25751PowerPathStatus/
  │   └── test_TPS25751PowerPathStatus.cpp
  ├── test_TPS25751BootFlags/
  │   └── test_TPS25751BootFlags.cpp
  ├── test_TPS25751PortConfig/
  │   └── test_TPS25751PortConfig.cpp
  ├── test_Factory/
  │   └── test_TPS25751RegisterFactory.cpp
  ├── test_Integration/
  │   └── test_MultiRegisterSequence.cpp
  └── README.md
```

### Naming Conventions

- Test directory: `test_[ClassName]/`
- Test file: `test_[ClassName].cpp`
- Test function: `test_[what_it_tests]()`
- Use snake_case for test function names

---

## Running Tests

### Local Testing

```bash
# Run all tests
pio test

# Run specific test environment
pio test -e teensy41

# Run specific test
pio test -f test_TPS25751Status

# Verbose output
pio test -v

# Run tests and see coverage
pio test --coverage
```

### Test Output

Expected output format:

```
test/test_TPS25751Status/test_TPS25751Status.cpp:45:test_default_constructor:PASS
test/test_TPS25751Status/test_TPS25751Status.cpp:52:test_data_constructor_valid:PASS
test/test_TPS25751Status/test_TPS25751Status.cpp:60:test_validate_basic:PASS
...
-----------------------
25 Tests 0 Failures 0 Ignored
OK
```

---

## Test Data Management

### Test Data Organization

Create reusable test data:

```cpp
namespace TestData {
    namespace Status {
        // Valid data samples
        constexpr uint8_t CONNECTED_DFP[] = {0x01, 0x01, 0x00, 0x00, 0x00};
        constexpr uint8_t CONNECTED_UFP[] = {0x02, 0x01, 0x01, 0x00, 0x00};
        constexpr uint8_t NOT_CONNECTED[] = {0x00, 0x00, 0x00, 0x00, 0x00};

        // Invalid data samples
        constexpr uint8_t RESERVED_BITS_SET[] = {0xC0, 0x00, 0x00, 0x00, 0x00};
        constexpr uint8_t INVALID_STATE[] = {0x0F, 0x00, 0x00, 0x00, 0x00};
    }

    namespace PowerPathStatus {
        // ... test data for power path status
    }
}
```

### Real Hardware Captures

Save actual hardware register dumps for testing:

```cpp
// Captured from real TPS25751 connected to Apple 87W charger
constexpr uint8_t APPLE_87W_SOURCE_CAPS[] = {
    0x03,  // 3 PDOs
    0x2C, 0x91, 0x01, 0x08,  // PDO1: 5V @ 3A
    0x0C, 0xB1, 0x02, 0x00,  // PDO2: 9V @ 3A
    0xB4, 0xB1, 0x03, 0x00,  // PDO3: 20V @ 4.35A
};
```

---

## Continuous Testing

### Pre-Commit Testing

Run tests before every commit:

```bash
# Add to .git/hooks/pre-commit
#!/bin/bash
echo "Running unit tests..."
pio test
if [ $? -ne 0 ]; then
    echo "Tests failed. Commit aborted."
    exit 1
fi
```

### Test Coverage Tracking

Monitor code coverage over time:

```bash
# Generate coverage report
pio test --coverage

# View coverage report
open .pio/test/coverage/index.html
```

### Regression Testing

Maintain a regression test suite:

```cpp
// test_regression.cpp
// Tests for previously discovered bugs to prevent regressions

void test_issue_001_status_vbus_parsing() {
    // Issue: VBUS bit was being read from wrong byte
    // Expected: byte 1, bit 0
    uint8_t data[] = {0x00, 0x01, 0x00, 0x00, 0x00};
    TPS25751Status status(data, 5);
    TEST_ASSERT_TRUE(status.isVBUSPresent());
}
```

---

## Success Criteria

Tests are successful when:

- ✅ All unit tests pass
- ✅ Code coverage > 90%
- ✅ Integration tests pass
- ✅ No compiler warnings in test code
- ✅ Hardware tests verify expected behavior
- ✅ Edge cases handled correctly
- ✅ Regression tests prevent old bugs

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial testing strategy document extracted from implementation plan |

---

*Comprehensive testing ensures library reliability and correctness.*
