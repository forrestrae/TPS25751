# TPS25751 Library - Testing Strategy

**Last Updated:** 2026-11-19
**Status:** Active

---

## Overview

This document defines the comprehensive testing strategy for the TPS25751 library. Our testing approach follows a multi-layered strategy: unit tests, integration tests, and hardware validation.

**The unit-test harness is real and running:** `pio test -e native` executes
host-run Unity tests (no hardware, no Arduino framework) from
`test/test_native/`. Every register batch must keep this run green — see the
[Implementation Plan](../plans/plan-impl-v01.md) per-batch loop. The `test/`
directory is excluded from the published registry tarball via `export.exclude`
in `library.json`.

**Quality Target:** >90% line coverage for all register implementations,
measured with `test/coverage.sh` (see Test Coverage Tracking). Not every file
meets it yet; the report lists the gaps.

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

### How the Harness Works

Unit tests run **on the host** via PlatformIO's `native` platform — no Teensy,
no `Wire`, no Arduino framework. The `[env:native]` environment in
`platformio.ini`:

- uses **Unity** as the test framework (`test_framework = unity`);
- compiles every register class, the base class, the debug helpers, and the
  factory via `build_src_filter` (`test_build_src = yes`), listed in register
  address order. The I2C-facing sources (`TPS25751.cpp`,
  `TPS25751DownstreamDevice.cpp`, `TPS25751PatchLoader.cpp`) are not compiled;
  a new register class must be added to the filter;
- satisfies the Arduino API symbols those sources reference (`Stream`,
  `Serial`, `F()`, `millis()`, `strncpy_P`) with the **header-only stubs** in
  `test/support/native/` (`Arduino.h`, `Stream.h`). The stubs print to stdout;
  they are just enough surface to compile the decode/encode logic, not an
  Arduino emulation. Extend them (minimally) if a new source file needs more;
- only runs suites under `test/test_native/` (`test_filter = test_native/*`);
- compiles as `-std=gnu++17`, the standard the Teensy build uses, so language
  features behave the same on the host and on the target. Without the flag the
  host default applies (Apple clang defaults to gnu++14), where attributes on
  enumerators such as the rev B aliases compile only as an extension.

PlatformIO defines **`PIO_UNIT_TESTING`** during test builds;
`TPS25751Register.h` uses it to make the protected `extractBits*` /
`extractEnum*` helpers public so tests can exercise them directly.
`test_TPS25751Register` covers those helpers, including fields past bit 255
(ADR-012).

To compare `debugPrint()` output, redirect stdout around the call; several
suites define a `captureDebugPrint()` helper that does this with `dup2()` and a
`tmpfile()`.

### Test Structure

Each register class gets its own suite directory under `test/test_native/`,
containing one `.cpp` with its own `main()` (native Unity tests provide their
own runner):

```
test/
  support/native/                  # Arduino API stubs (not a test suite)
  test_native/
    test_TPS25751Status/
      test_TPS25751Status.cpp      # seed suite - use as the reference example
    test_TPS25751<NewClass>/
      test_TPS25751<NewClass>.cpp
```

**`test/test_native/test_TPS25751Status/test_TPS25751Status.cpp` is the
canonical example** — it was written against the real register API and verified
green. Pattern-match it rather than inventing structure.

### Required Test Cases

For every register class:

1. **Construction** — default ctor (allocates zeroed buffer of the register's
   `RegisterInfo` size), data ctor (deep-copies the buffer), null-data and
   wrong-size behavior.
2. **Validation tiers** — `isValid()` / `isValidSize()` (basic),
   `hasValidData()` (data; all-`0xFF` is the bus-error signature), and
   `isSemanticallyValid()` (semantic; one test per logical-consistency rule,
   both passing and failing).
3. **Field decode** — at least one hand-packed buffer per meaningful register
   state, asserting **every** accessor. Pack the bytes from the TRM bit
   offsets (cross-checked against the `tps25751-docs` MCP server) and comment
   the packing arithmetic in the test file.
4. **Encode round-trip** (R/W registers only) — set-field → raw-bytes →
   re-decode.
5. **Rule of Five** — copy preserves and deep-copies data; move transfers the
   buffer and leaves the source `!isValid()`.

Base-class behavior (`extractBits*` bounds checking, cross-byte extraction)
lives in the STATUS seed suite and does not need repeating per register.

### Test Skeleton

```cpp
#include <unity.h>
#include "TPS25751Status.h"          // the class under test
#include "TPS25751RegisterAddress.h"

namespace {
constexpr size_t SIZE = TPS25751Registers::Registers::STATUS.size;
// Hand-packed from TRM bit offsets - comment the arithmetic:
//   byte0 = 1 | (6 << 1) | (1 << 4) = 0x1D  (plug, state 6, CC2)
constexpr uint8_t CONNECTED[SIZE] = {0x1D, 0x00, 0xD0, 0x48, 0x00};
}

void setUp() {}
void tearDown() {}

void test_connected_decode()
{
    TPS25751Status status(CONNECTED);
    TEST_ASSERT_TRUE(status.plugPresent());
    TEST_ASSERT_EQUAL(CONNECTED_NO_RA_DETECTED, status.connectionState());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_connected_decode);
    return UNITY_END();
}
```

---

## Integration Testing

> **Status: design guidance, not yet implemented.** No mock-`Wire` layer or
> integration suites exist yet; the sketches below are targets for when they
> are added (they would live under `test/test_native/` and run in the same
> native env, with the mock replacing the stub transport).

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

`test_TPS25751RegisterFactory` runs natively. For every address in the TRM it
checks that each of the three `createRegister()` overloads returns a non-null
object of the size in `getRegisterInfo()`. With no RTTI on the target, it
checks the class by behavior: the factory object's `debugPrint()` output must
equal that of the expected class constructed from the same bytes. A sweep over
all 256 address byte values checks that the factory and `getRegisterInfo()`
agree on which addresses exist. A new register needs one line in the suite's
`kAll` table, mapping its `Address` to its class; the suite then fails until
all three factory switches have the case.

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

### 4CC Command-Task / I2Cc Proxy Test Cases (required)

These cover the write path, executor, and downstream-device codec added for the
4CC command-task interface (TRM Sec 4.4.2/4.4.3). Required, regardless of unit
test harness availability:

- **Write-path framing**: mock `Wire` and assert `writeRegister()` sends
  `[regAddr][length][data...]` — i.e. the length byte is *sent*, mirroring the
  read path's *received* length byte. Assert failure is returned (and no crash)
  when `Wire.write()`'s return value is short or `endTransmission()` is non-zero.
- **Executor reject path**: mock COMMAND read-back as `"!CMD"` after a write;
  assert `executeCommand()` returns `TPS25751TaskStatus::Rejected`, not `Success`
  or `Timeout`.
- **Executor success path**: mock COMMAND clearing to all-zero after N polls and
  DATA containing a known payload; assert `TPS25751TaskResult.returnCode == DATA[0]`
  and `outData` is filled starting at `DATA[0]` (not offset 1 — that offset is an
  I2Cr-specific convention applied by the caller, not by `executeCommand()` itself).
- **Executor timeout path**: mock COMMAND that never clears/rejects; assert
  `TPS25751TaskStatus::Timeout` after `timeoutMs` and that the loop actually exits
  (no hang).
- **I2Cr encode/decode round-trip**: assert `TPS25751DownstreamDevice::readRegister()`
  builds the 3-byte input `{addr, devReg, len}` correctly (including masking bit 7
  off `addr`), and that returned bytes are copied from **DATA offset 1**
  (`raw[1..len]`), not offset 0.
- **I2Cw encode/decode round-trip**: assert the input payload is
  `{addr, len, 0, devReg, payload...}` and that `len > 10` is rejected by the
  caller before ever reaching `executeCommand()`.
- **5-second spacing (logical, non-hardware)**: assert `TPS25751DownstreamDevice`
  logs/warns (rather than blocking) when two same-type (I2Cr or I2Cw) calls are
  issued less than 5 s apart, using an injectable/mockable `millis()` if the test
  harness supports it.

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

### I2Cc Downstream-Device Proxy Hardware Checklist

Requires a real device on the TPS25751's I2Cc bus (e.g. a BQ25798 charger) and,
ideally, a logic analyzer on the I2Cc lines:

- [ ] `executeCommand(TPS25751FourCC::of("!XYZ"))` smoke test returns `Rejected`
      (no device required; validates the COMMAND/DATA handshake itself)
- [ ] `TPS25751DownstreamDevice::readRegister()` against a known-value register
      (e.g. a part-ID register) returns the expected bytes
- [ ] Logic-analyzer capture confirms the I2Cr input payload on the wire matches
      `{addr, devReg, len}` and that read bytes really start at DATA offset 1
- [ ] `TPS25751DownstreamDevice::writeRegister()` against a scratch/writable
      downstream register, confirmed via a subsequent read or logic-analyzer
      capture — this also confirms the I2Cw "Length" field's exact interpretation
      (the TRM wording leaves whether it counts the register-offset byte mildly
      ambiguous; resolve against the capture, not the TRM text alone)
- [ ] Two consecutive I2Cr calls (or two consecutive I2Cw calls) issued <5 s apart
      produce the `DEBUG_CAT_TASK` warning and confirm the call still proceeds
      (not hard-blocked)
- [ ] 7-bit target address masking verified against the capture (bit 7 of the
      address byte is 0 on the wire even if a caller passes an 8-bit value in)

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
  ├── support/
  │   └── native/                       # Arduino API stubs for [env:native]
  │       ├── Arduino.h
  │       └── Stream.h
  ├── test_native/                      # host-run suites: pio test -e native
  │   ├── test_TPS25751Status/
  │   │   └── test_TPS25751Status.cpp   # seed suite (reference example)
  │   └── test_TPS25751<Class>/
  │       └── test_TPS25751<Class>.cpp
  └── README
```

Directories that do not start with `test_` (like `support/`) are ignored by the
test runner. Embedded/on-target suites, if ever added, would live in a sibling
`test_embedded/` tree and a matching test env — none exist today.

### Naming Conventions

- Test directory: `test_native/test_[ClassName]/`
- Test file: `test_[ClassName].cpp`
- Test function: `test_[what_it_tests]()`
- Use snake_case for test function names

---

## Running Tests

### Local Testing

```bash
# Run all native unit tests (the standard command)
pio test -e native

# Run one suite
pio test -e native -f "test_native/test_TPS25751Status"

# Verbose output (shows the stubs' stdout, e.g. debugPrint output)
pio test -e native -v
```

`pio test` without `-e native` targets the hardware example environments
(`default_envs`) and will try to upload to a Teensy — always pass `-e native`
for unit tests.

### Test Output

Expected output format (from the seed suite):

```
test/test_native/test_TPS25751Status/test_TPS25751Status.cpp:285: test_default_constructor_allocates_zeroed_buffer	[PASSED]
...
=================================== SUMMARY ===================================
Environment    Test                             Status    Duration
-------------  -------------------------------  --------  ------------
native         test_native/test_TPS25751Status  PASSED    00:00:04.911
================= 22 test cases: 22 succeeded in 00:00:04.911 =================
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
pio test -e native
if [ $? -ne 0 ]; then
    echo "Tests failed. Commit aborted."
    exit 1
fi
```

### Test Coverage Tracking

`test/coverage.sh` measures line coverage of the native suites and prints one
line per file in `src/`, sorted with the least-covered file first. Run it from
the library root:

```bash
test/coverage.sh
```

Pass `--html` to also write a per-line report to `.pio/coverage/index.html`.
The script needs gcovr (`brew install gcovr` or `pipx install gcovr`). It
builds `[env:native-coverage]`, which extends `[env:native]` with `--coverage`
and `-O0`; `test/support/coverage_link.py` adds `--coverage` to the link step,
which `build_flags` does not reach. The script rebuilds that environment from
scratch with an empty build cache on every run, because counters accumulate
across runs and the shared cache in `.pio/buildcache` restores object files
without the `.gcno` notes gcov needs. On macOS it reads Apple clang's counters
with `xcrun llvm-cov gcov`.

Only sources compiled by `[env:native]` appear in the report, so the I2C-facing
`TPS25751.cpp`, `TPS25751DownstreamDevice.cpp`, and `TPS25751PatchLoader.cpp`
are not measured. Neither the verify gate nor CI runs the coverage step; the
>90% target is checked by running the script at review.

First measurement, 2026-11-19 (727 tests): 84% of lines overall. Files under
90%, lowest first: `TPS25751PowerStatus.cpp` 11%, `TPS25751Debug.cpp` 26%,
`TPS25751PDStatus.cpp` 36%, `TPS25751TypeCState.cpp` 38%, `TPS25751Data.cpp`
42%, `TPS25751PowerPathStatus.cpp` 51%, `TPS25751InterruptClear.cpp` 54%,
`TPS25751Mode.cpp` 56%, `TPS25751Command.cpp` 63%, `TPS25751BootFlags.cpp` 68%,
`TPS25751ReceivedSinkCaps.cpp` 74%, `TPS25751TransmitSinkCaps.cpp` 77%,
`TPS25751ReceivedSourceCaps.cpp` 78%, `TPS25751PortConfig.cpp` 82%,
`TPS25751PortControl.cpp` 83%, `TPS25751Status.cpp` 84%,
`TPS25751InterruptMask.cpp` 85%, and `TPS25751TransmitSourceCaps.cpp` 86%. Nine
of the ten lowest have no suite of their own and are reached only through the
factory suite; the rest miss mostly `debugPrint()` branches and semantic-check
failure paths.

### Regression Testing

Maintain a regression test suite:

```cpp
// test_regression.cpp
// Tests for previously discovered bugs to prevent regressions

void test_issue_001_status_vbus_parsing() {
    // Issue: VBUS bit was being read from wrong byte
    // Expected: byte 1, bit 0
    uint8_t data[] = {0x00, 0x00, 0x10, 0x00, 0x00};  // bit 20 set
    TPS25751Status status(data, 5);
    TEST_ASSERT_EQUAL(VBUS_AT_VSAFE5V, status.vbusStatus());
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
| 1.1 | 2026-07-06 | Claude Code | Batch 0: replaced aspirational unit-test sections with the real, working `pio test -e native` harness (Unity, native env, Arduino stubs in `test/support/native/`, seed suite for TPS25751Status); corrected fictional API in examples; marked coverage tooling as pending |
| 1.2 | 2026-11-19 | Claude Code | The native build compiles every register class and the factory; factory and base-class suites; line coverage with `test/coverage.sh` and `[env:native-coverage]`, with the first per-file numbers |

---

*Comprehensive testing ensures library reliability and correctness.*
