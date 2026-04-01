# TPS25751 Library - Documentation Requirements

**Last Updated:** 2025-10-20
**Status:** Active

---

## Overview

This document defines the documentation requirements for the TPS25751 library. Comprehensive documentation ensures the library is accessible, maintainable, and usable by both current and future developers.

---

## Table of Contents

1. [Code Documentation](#code-documentation)
2. [Example Sketches](#example-sketches)
3. [Troubleshooting Guides](#troubleshooting-guides)
4. [API Reference](#api-reference)
5. [Documentation Standards](#documentation-standards)

---

## Code Documentation

### Doxygen Requirements

All public APIs MUST have complete Doxygen documentation.

#### Class Documentation

```cpp
/**
 * @file TPS25751Status.h
 * @brief STATUS register (0x1A) class implementation
 *
 * The STATUS register provides real-time information about the Type-C connection
 * state, VBUS status, power role, data role, and plug orientation.
 *
 * This register is read-only and reflects the current hardware state. It should
 * be polled regularly or read in response to interrupts to monitor connection
 * status changes.
 *
 * **Register Details:**
 * - Address: 0x1A
 * - Size: 5 bytes
 * - Access: Read-only
 *
 * **Typical Usage:**
 * @code
 * TPS25751 tps;
 * auto status = tps.readStatusRegister();
 * if (status && status->isVBUSPresent()) {
 *     Serial.println("VBUS is present");
 *     Serial.print("Connection state: ");
 *     Serial.println(status->getConnectionStateString());
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual, Section 8.6.2.2
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751Status : public TPS25751Register {
    // ...
};
```

#### Method Documentation

```cpp
/**
 * @brief Get the Type-C connection state
 *
 * Returns the current state of the Type-C state machine, indicating whether
 * a device is connected and in what role (DFP/UFP).
 *
 * **Connection States:**
 * | Value | State | Description |
 * |-------|-------|-------------|
 * | 0x00 | Not Connected | No device attached |
 * | 0x01 | Connected.DFP | Connected as Downstream Facing Port |
 * | 0x02 | Connected.UFP | Connected as Upstream Facing Port |
 * | 0x03 | Connected.Debug DFP | Debug accessory mode (DFP) |
 * | 0x04 | Connected.Debug UFP | Debug accessory mode (UFP) |
 * | 0x05 | Audio Accessory | Audio adapter mode |
 * | 0x06-0x07 | Reserved | Invalid/undefined states |
 *
 * **Typical values:**
 * - When acting as a USB device: 0x02 (UFP)
 * - When acting as a USB host: 0x01 (DFP)
 * - When disconnected: 0x00
 *
 * @return uint8_t Connection state value (0-7)
 *
 * @note Changes to this value typically trigger INT_EVENT1 interrupt
 * @see getConnectionStateString() for human-readable string
 * @see TI TPS25751 Technical Reference Manual, Section 8.6.2.2, Table 8-35
 *
 * **Example:**
 * @code
 * auto status = tps.readStatusRegister();
 * uint8_t state = status->getConnectionState();
 * if (state == 0x02) {
 *     Serial.println("Connected as UFP (device mode)");
 * }
 * @endcode
 */
uint8_t getConnectionState() const;
```

#### Field Documentation

```cpp
/**
 * @brief Check if VBUS voltage is present
 *
 * Indicates whether VBUS voltage is detected on the USB-C connector.
 * This typically means a charger or host is connected and providing power.
 *
 * **When VBUS is present:**
 * - A power source is connected
 * - Voltage is above detection threshold (~4V typically)
 * - Power negotiation may be in progress
 *
 * **When VBUS is absent:**
 * - No power source connected, or
 * - Voltage below detection threshold, or
 * - Cable disconnected
 *
 * @return true if VBUS voltage is detected
 * @return false if VBUS voltage is not detected
 *
 * @note VBUS presence doesn't indicate the voltage level, only that voltage exists
 * @see getPowerRole() to determine if we're sourcing or sinking power
 * @see TI TPS25751 Technical Reference Manual, Section 8.6.2.2, bit field VBUS_PRESENT
 *
 * **Example:**
 * @code
 * auto status = tps.readStatusRegister();
 * if (status->isVBUSPresent()) {
 *     Serial.println("Power source detected");
 * } else {
 *     Serial.println("No power - check cable connection");
 * }
 * @endcode
 */
bool isVBUSPresent() const;
```

### Documentation Elements

Every public method must include:

1. **@brief** - One-line summary
2. **Description** - Detailed explanation of functionality
3. **@param** - For each parameter (if any)
4. **@return** - Return value description
5. **@note** - Important usage notes
6. **@see** - Related functions/documentation
7. **@code / @endcode** - Usage example
8. **Datasheet reference** - Section numbers

### Private Method Documentation

Private methods should have brief documentation:

```cpp
/**
 * @brief Extract connection state bitfield from raw data
 * @return Connection state value (0-7)
 */
uint8_t extractConnectionState() const;
```

---

## Example Sketches

### Required Examples

Every major feature must have a working example sketch:

#### 1. Basic Register Reading

**File:** `examples/basic_register_read/basic_register_read.ino`

```cpp
/**
 * @file basic_register_read.ino
 * @brief Demonstrates reading basic TPS25751 registers
 *
 * This example shows how to:
 * - Initialize the TPS25751 library
 * - Read STATUS register
 * - Read MODE register
 * - Print formatted register values
 *
 * Hardware Requirements:
 * - Teensy 4.0 or 4.1
 * - TPS25751 connected via I2C (Wire1)
 * - USB-C cable for Serial monitor
 *
 * Connections:
 * - SDA: Teensy pin 18 -> TPS25751 SDA
 * - SCL: Teensy pin 19 -> TPS25751 SCL
 *
 * Expected Output:
 * - Connection state
 * - VBUS status
 * - Power role
 * - Operating mode
 */

#include <TPS25751.h>

TPS25751 tps;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);  // Wait up to 3 seconds

    Serial.println("TPS25751 Basic Register Read Example");
    Serial.println("=====================================");

    if (!tps.begin()) {
        Serial.println("ERROR: Failed to initialize TPS25751");
        Serial.println("Check I2C connections and address");
        while (1) delay(100);
    }

    Serial.println("TPS25751 initialized successfully");
}

void loop() {
    // Read STATUS register
    auto status = tps.readStatusRegister();
    if (status) {
        Serial.println("\n--- STATUS Register ---");
        Serial.print("Connection State: ");
        Serial.println(status->getConnectionStateString());

        Serial.print("VBUS Present: ");
        Serial.println(status->isVBUSPresent() ? "Yes" : "No");

        Serial.print("Power Role: ");
        Serial.println(status->getPowerRole() == 0 ? "Sink" : "Source");
    }

    // Read MODE register
    auto mode = tps.readModeRegister();
    if (mode) {
        Serial.println("\n--- MODE Register ---");
        mode->debugPrint();
    }

    delay(2000);  // Update every 2 seconds
}
```

#### 2. Power Delivery Monitoring

**File:** `examples/pd_monitoring/pd_monitoring.ino`

- Monitor active PDO/RDO contracts
- Display negotiated voltage/current
- Show source capabilities
- Track power negotiation changes

#### 3. Interrupt Handling

**File:** `examples/interrupt_handling/interrupt_handling.ino`

- Set up interrupt pin
- Configure interrupt masks
- Handle interrupt events
- Clear interrupts

#### 4. GPIO Control

**File:** `examples/gpio_control/gpio_control.ino`

- Read GPIO status
- Configure GPIO modes
- Control GPIO outputs

#### 5. Port Control

**File:** `examples/port_control/port_control.ino`

- Read port configuration
- Modify port settings
- Force role changes
- Enable/disable features

### Example Documentation Structure

Each example should include:

```cpp
/**
 * @example example_name.ino
 *
 * @brief Brief description
 *
 * Detailed description of what the example demonstrates.
 *
 * @section hardware Hardware Requirements
 * - List of required hardware
 *
 * @section connections Connections
 * - Pin connection details
 *
 * @section usage Usage Instructions
 * 1. Step-by-step instructions
 *
 * @section expected Expected Output
 * - Description of expected behavior
 *
 * @section troubleshooting Troubleshooting
 * - Common issues and solutions
 */
```

---

## Troubleshooting Guides

### User-Facing Troubleshooting

**File:** `docs/TROUBLESHOOTING.md`

#### Connection Issues

```markdown
# Troubleshooting Guide

## Connection Issues

### Problem: "Failed to initialize TPS25751"

**Symptoms:**
- `tps.begin()` returns false
- Serial output shows initialization error

**Possible Causes:**
1. I2C wiring incorrect
2. Wrong I2C address
3. TPS25751 not powered
4. I2C bus conflict

**Solutions:**

1. **Check I2C Wiring**
   - Verify SDA connection (Teensy pin 18 -> TPS25751 SDA)
   - Verify SCL connection (Teensy pin 19 -> TPS25751 SCL)
   - Check for loose connections

2. **Verify I2C Address**
   ```cpp
   // Try scanning I2C bus
   #include <Wire.h>

   void scanI2C() {
       for (byte addr = 1; addr < 127; addr++) {
           Wire.beginTransmission(addr);
           if (Wire.endTransmission() == 0) {
               Serial.print("Found device at 0x");
               Serial.println(addr, HEX);
           }
       }
   }
   ```

3. **Check Power Supply**
   - Verify 3.3V power to TPS25751
   - Check for voltage on VDD pin
   - Ensure ground connection

4. **Test I2C Communication**
   ```cpp
   Wire.beginTransmission(0x20);  // Default address
   byte error = Wire.endTransmission();
   if (error == 0) {
       Serial.println("I2C communication OK");
   } else {
       Serial.print("I2C error: ");
       Serial.println(error);
   }
   ```
```

### Developer Troubleshooting

**File:** `docs/engineering/TROUBLESHOOTING_DEV.md`

#### Compilation Issues

```markdown
## Compilation Errors

### Error: "dynamic_cast not supported"

**Cause:** Platform compiled with `-fno-rtti`

**Solution:** Use `static_cast` instead
```cpp
// Wrong
auto* status = dynamic_cast<TPS25751Status*>(ptr);

// Right
auto* status = static_cast<TPS25751Status*>(ptr);
```

### Error: "static_assert failed"

**Cause:** Template `static_assert(false)` instantiated during parsing

**Solution:** Use dependent type
```cpp
// Wrong
template<typename T>
void foo() { static_assert(false, "msg"); }

// Right
template<typename T>
struct dependent_false : std::false_type {};

template<typename T>
void foo() { static_assert(dependent_false<T>::value, "msg"); }
```
```

### Power Delivery Troubleshooting

```markdown
## Power Delivery Issues

### Problem: Negotiation Stuck at 5V

**Symptoms:**
- Device only charges at 5V
- Higher voltages not negotiated
- Source capabilities show multiple PDOs

**Debug Steps:**

1. **Check Source Capabilities**
   ```cpp
   auto caps = tps.readReceivedSourceCapabilities();
   caps->debugPrint();  // Shows all available PDOs
   ```

2. **Verify Sink Request**
   ```cpp
   auto rdo = tps.readActiveRDOContract();
   rdo->debugPrint();  // Shows what was requested
   ```

3. **Check Contract**
   ```cpp
   auto pdo = tps.readActivePDOContract();
   Serial.print("Negotiated voltage: ");
   Serial.print(pdo->getVoltageMillivolts());
   Serial.println("mV");
   ```

4. **Common Causes:**
   - Sink capabilities not advertising higher voltages
   - Cable doesn't support USB PD
   - Source/sink capability mismatch
   - E-marked cable required for >3A
```

---

## API Reference

### Auto-Generated Documentation

Use Doxygen to generate comprehensive API reference:

**File:** `Doxyfile`

```
PROJECT_NAME           = "TPS25751 Library"
PROJECT_BRIEF          = "USB-C Power Delivery Controller Library for Teensy"
OUTPUT_DIRECTORY       = docs/api
INPUT                  = lib/TPS25751/include lib/TPS25751/src
RECURSIVE              = YES
EXTRACT_ALL            = YES
EXTRACT_PRIVATE        = NO
EXTRACT_STATIC         = YES
GENERATE_HTML          = YES
GENERATE_LATEX         = NO
HTML_OUTPUT            = html
```

### Generate Documentation

```bash
# Install Doxygen
# macOS: brew install doxygen
# Linux: apt-get install doxygen

# Generate docs
doxygen Doxyfile

# View docs
open docs/api/html/index.html
```

---

## Documentation Standards

### Writing Style

**Be Clear and Concise:**
- Use active voice
- Short sentences
- Avoid jargon unless necessary
- Define acronyms on first use

**Be Specific:**
```cpp
// Vague
/// Gets the state

// Specific
/// Get the Type-C connection state (0x00-0x07)
```

**Provide Context:**
```cpp
// Incomplete
/// @return Connection state value

// Complete
/// @return Connection state value (0-7)
///         0x00 = Not Connected
///         0x01 = Connected.DFP (host mode)
///         0x02 = Connected.UFP (device mode)
```

### Code Examples

**All examples must:**
- Compile without errors
- Run on actual hardware
- Demonstrate best practices
- Include error handling
- Show expected output

**Bad example:**
```cpp
// No context, no error handling
auto reg = tps.read();
Serial.println(reg.value);
```

**Good example:**
```cpp
// Read STATUS register with error handling
auto status = tps.readStatusRegister();
if (status && status->validateData()) {
    Serial.print("Connection State: ");
    Serial.println(status->getConnectionStateString());
} else {
    Serial.println("Error: Failed to read STATUS register");
}
```

### Datasheet References

**Always reference datasheet sections:**

```cpp
/**
 * @brief Get VBUS voltage status
 *
 * [description]
 *
 * @see TI TPS25751 Technical Reference Manual
 *      Section 8.6.2.2 "STATUS Register"
 *      Table 8-35, bit field VBUS_PRESENT
 */
```

### Version Documentation

Track API changes:

```cpp
/**
 * @brief Get connection state
 *
 * @return Connection state value (0-7)
 *
 * @since v1.0.0
 * @deprecated Since v2.0.0, use getTypeCState() instead
 */
```

---

## Documentation Checklist

For every new register class:

- [ ] Class has Doxygen @file comment
- [ ] Class has @brief description
- [ ] Class has detailed description
- [ ] Class has usage example in @code block
- [ ] Class references datasheet section
- [ ] Every public method has @brief
- [ ] Every public method has detailed description
- [ ] Every parameter has @param documentation
- [ ] Every return value has @return documentation
- [ ] Methods include usage examples
- [ ] Methods reference datasheet bit fields
- [ ] Example sketch created and tested
- [ ] Troubleshooting section added (if applicable)
- [ ] API reference generated with Doxygen

---

## Documentation Maintenance

### Regular Reviews

- Review documentation quarterly
- Update for API changes immediately
- Keep examples tested and working
- Update troubleshooting based on user feedback

### User Feedback

- Track documentation issues on GitHub
- Maintain FAQ based on common questions
- Update examples based on user requests
- Clarify confusing sections

---

## Success Criteria

Documentation is successful when:

- ✅ Doxygen generates without warnings
- ✅ All public APIs documented
- ✅ Examples compile and run on hardware
- ✅ Users can get started without asking questions
- ✅ Troubleshooting guide covers common issues
- ✅ Code examples demonstrate best practices
- ✅ Datasheet references accurate and complete

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial documentation requirements extracted from implementation plan |

---

*Comprehensive documentation makes the library accessible and maintainable.*
