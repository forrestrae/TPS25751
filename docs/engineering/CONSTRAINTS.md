# TPS25751 Library - Technical Constraints

**Last Updated:** 2025-10-20
**Status:** Active

---

## Overview

This document details the technical constraints and platform-specific considerations that affect the TPS25751 library implementation. Understanding these constraints is critical for writing code that compiles and runs correctly on the target platform.

---

## Table of Contents

1. [Platform Constraints](#platform-constraints)
2. [I2C Protocol Details](#i2c-protocol-details)
3. [Register Access Patterns](#register-access-patterns)
4. [USB Power Delivery Complexity](#usb-power-delivery-complexity)
5. [Memory Considerations](#memory-considerations)
6. [Common Pitfalls](#common-pitfalls)

---

## Platform Constraints

### Teensy 4.x Embedded Platform

The library targets Teensy 4.0 and 4.1 microcontrollers with the following characteristics:

**Processor:**
- ARM Cortex-M7 @ 600 MHz
- 1 MB Flash
- 512 KB RAM (Teensy 4.0) / 1024 KB RAM (Teensy 4.1)

**Implications:**
- Limited memory compared to desktop systems
- No virtual memory or paging
- Real-time constraints
- Limited standard library support

### No RTTI (Run-Time Type Information)

**CRITICAL:** The platform compiles with `-fno-rtti` flag.

**What this means:**
- `dynamic_cast<T>()` will **NOT** compile
- `typeid()` will **NOT** compile
- Virtual function overhead reduced
- Smaller binary size

**❌ NEVER do this:**
```cpp
// COMPILATION ERROR - dynamic_cast requires RTTI
auto* status = dynamic_cast<TPS25751Status*>(reg.get());

// COMPILATION ERROR - typeid requires RTTI
if (typeid(*reg) == typeid(TPS25751Status)) { }
```

**✅ ALWAYS do this:**
```cpp
// Use static_cast, dispatching on the register's address
if (reg->getAddress() == static_cast<uint8_t>(TPS25751Registers::Address::STATUS)) {
    auto* status = static_cast<TPS25751Status*>(reg.get());
    // Now safe to use status
}

// Or use the factory directly (keyed on Address)
auto status = TPS25751Factory::getInstance()
    .createRegister(TPS25751Registers::Address::STATUS);
auto* statusPtr = static_cast<TPS25751Status*>(status.get());
```

**Why this matters:**
- Code must rely on factory pattern for type safety
- Use address-based type identification
- Design must work without runtime type queries

### Template Constraints

**Problem:** `static_assert(false)` in template causes immediate compilation failure

**❌ DON'T:**
```cpp
template<typename T>
void processRegister(T* reg) {
    static_assert(false, "Not implemented");  // FAILS IMMEDIATELY!
}
```

**Why:** The template is instantiated during parsing, and `static_assert(false)` always fails, even if the function is never called.

**✅ DO - Use SFINAE instead:**
```cpp
// Option 1: Type traits
template<typename T>
struct is_register : std::false_type {};

template<>
struct is_register<TPS25751Status> : std::true_type {};

template<typename T>
typename std::enable_if<is_register<T>::value, void>::type
processRegister(T* reg) {
    // Implementation for valid register types
}

// Option 2: Dependent type
template<typename T>
struct dependent_false : std::false_type {};

template<typename T>
void processRegister(T* reg) {
    static_assert(dependent_false<T>::value, "Not implemented");
}
```

### Limited C++ Standard Library

**Available:**
- Basic STL containers (`std::vector`, `std::array`, `std::unique_ptr`)
- Standard algorithms
- Memory management (`std::move`, `std::forward`)
- Type traits

**Limited/Unavailable:**
- Some C++17/20 features
- Full iostream library (use Serial instead)
- Thread support
- Filesystem support

**Best Practices:**
- Test on actual hardware, not just desktop compiler
- Avoid bleeding-edge C++ features
- Use Arduino-compatible patterns where possible

### Arduino Environment Integration

**Serial Output:**
```cpp
// Use Serial, not std::cout
Serial.println("Status");

// Use F() macro to save RAM
Serial.println(F("This string is stored in flash"));
```

**Timing:**
```cpp
// Use Arduino functions
delay(1000);           // milliseconds
delayMicroseconds(5);  // microseconds
millis();              // milliseconds since boot
micros();              // microseconds since boot
```

**I2C:**
```cpp
// Use Wire library
Wire.begin();
Wire.beginTransmission(address);
Wire.write(data);
Wire.endTransmission();
```

---

## I2C Protocol Details

### TPS25751 Custom I2C Protocol

The TPS25751 uses a **non-standard** I2C protocol:

**Standard I2C read:**
```
START | ADDR+W | REG | START | ADDR+R | DATA... | STOP
```

**TPS25751 I2C read:**
```
START | ADDR+W | REG | START | ADDR+R | LENGTH | DATA... | STOP
                                        ^^^^^^^
                                        First byte is length!
```

**Key Difference:** The first byte read is the **byte count**, not data.

### Reading Registers

**Correct implementation:**
```cpp
bool TPS25751::readRegister(uint8_t regAddr, uint8_t* buffer, size_t bufferSize) {
    // Write register address
    Wire.beginTransmission(i2cAddress_);
    Wire.write(regAddr);
    if (Wire.endTransmission(false) != 0) {  // Repeated start
        return false;
    }

    // Read length byte first
    Wire.requestFrom(i2cAddress_, 1);
    if (Wire.available() < 1) {
        return false;
    }
    uint8_t length = Wire.read();

    // Validate length
    if (length > bufferSize) {
        return false;
    }

    // Read actual data
    Wire.requestFrom(i2cAddress_, length);
    for (size_t i = 0; i < length && Wire.available(); i++) {
        buffer[i] = Wire.read();
    }

    return true;
}
```

**Common mistake:**
```cpp
// WRONG - ignores length byte
Wire.requestFrom(addr, 5);  // Reads length + 4 bytes of data
buffer[0] = Wire.read();     // This is the LENGTH, not data!
```

### Maximum Read Sizes

Different registers have different maximum sizes:

| Register | Max Size | Notes |
|----------|----------|-------|
| STATUS | 5 bytes | Fixed size |
| MODE | 4 bytes | Fixed size |
| DATA | 64 bytes | **Largest register** |
| RECEIVED_SOURCE_CAPABILITIES | 29 bytes | Variable (1 + 7*4) |
| IO_CONFIG | 48 bytes | Large configuration |

**Implication:** Buffers must accommodate largest expected register.

### I2C Timing

**Clock Speed:** Typically 100 kHz or 400 kHz
**Considerations:**
- Slower speeds more reliable with longer cables
- Faster speeds reduce read latency
- Some operations require delay between transactions

```cpp
Wire.setClock(400000);  // 400 kHz
```

---

## Register Access Patterns

### Read-Only Registers (R)

**Examples:** STATUS, MODE, BOOT_STATUS, RECEIVED_SOURCE_CAPABILITIES

**Characteristics:**
- Can only be read
- Writing has no effect or may cause errors
- Reflect current hardware state

**Implementation:**
```cpp
bool isReadOnly() const override {
    return true;
}
```

### Write-Only Registers (W)

**Examples:** COMMAND, INT_CLEAR1

**Characteristics:**
- Can only be written
- Reading returns undefined values
- Used for triggering actions

**Implementation:**
```cpp
bool isReadOnly() const override {
    return false;  // Can be written
}

// Reading may not be meaningful
bool validateData() const override {
    // Skip data validation for write-only registers
    return true;
}
```

**Usage pattern:**
```cpp
TPS25751Command cmd;
cmd.setCommand(CommandCode::SOFTWARE_RESET);
tps.writeRegister(cmd);
// Don't try to read back - it's write-only!
```

### Read/Write Registers (R/W)

**Examples:** PORT_CONTROL, INT_MASK1, IO_CONFIG

**Characteristics:**
- Can be read and written
- Reading returns last written value or current state
- Support read-modify-write patterns

**Implementation:**
```cpp
bool isReadOnly() const override {
    return false;
}

// Support both reading and writing
bool validateData() const override {
    // Full validation
    return checkReservedBits() && checkFieldRanges();
}
```

**Usage pattern:**
```cpp
// Read-modify-write
auto portControl = tps.readPortControl();
portControl->setDataRoleSwapEnabled(true);
tps.writePortControl(*portControl);
```

---

## USB Power Delivery Complexity

### PDO (Power Data Object) Types

USB PD defines 4 PDO types, each with different bit layouts:

#### 1. Fixed Supply PDO (Type 00)

```
Bits 31-30: 00 (Fixed Supply)
Bits 29-28: Dual-Role Power
Bits 27-26: USB Suspend Supported
Bit  25:    Unconstrained Power
Bit  24:    USB Communications Capable
Bits 23-22: Dual-Role Data
Bits 21-20: Unchunked Extended Messages Supported
Bit  19-10: Voltage in 50mV units
Bits 9-0:   Maximum current in 10mA units
```

**Voltage calculation:**
```cpp
uint16_t voltageBits = (pdo >> 10) & 0x3FF;
uint16_t voltageMillivolts = voltageBits * 50;
```

**Current calculation:**
```cpp
uint16_t currentBits = pdo & 0x3FF;
uint16_t currentMilliamps = currentBits * 10;
```

#### 2. Battery PDO (Type 01)

```
Bits 31-30: 01 (Battery)
Bits 29-20: Maximum Voltage in 50mV units
Bits 19-10: Minimum Voltage in 50mV units
Bits 9-0:   Maximum Allowable Power in 250mW units
```

**Power calculation:**
```cpp
uint16_t powerBits = pdo & 0x3FF;
uint32_t powerMilliwatts = powerBits * 250;
```

#### 3. Variable Supply PDO (Type 10)

```
Bits 31-30: 10 (Variable)
Bits 29-20: Maximum Voltage in 50mV units
Bits 19-10: Minimum Voltage in 50mV units
Bits 9-0:   Maximum current in 10mA units
```

#### 4. Augmented PDO / PPS (Type 11)

```
Bits 31-30: 11 (Augmented)
Bits 29-28: 00 (PPS)
Bits 27:    PPS Power Limited
Bits 26-17: Maximum Voltage in 100mV units  ← Different unit!
Bits 16-8:  Minimum Voltage in 100mV units  ← Different unit!
Bits 7-0:   Maximum Current in 50mA units   ← Different unit!
```

**Note:** PPS uses **different units** than other PDO types!

### PDO Parsing Strategy

**Type detection:**
```cpp
uint8_t pdoType = (pdo >> 30) & 0x03;

switch (pdoType) {
    case 0b00:  // Fixed Supply
        return parseFixedSupplyPDO(pdo);
    case 0b01:  // Battery
        return parseBatteryPDO(pdo);
    case 0b10:  // Variable Supply
        return parseVariablePDO(pdo);
    case 0b11:  // Augmented (PPS)
        return parseAugmentedPDO(pdo);
}
```

### RDO (Request Data Object) Structure

```
Bits 31-28: Object Position (which PDO from 1-7)
Bit  27:    Give Back Flag
Bit  26:    Capability Mismatch
Bit  25:    USB Communications Capable
Bit  24:    No USB Suspend
Bit  23:    Unchunked Extended Messages Supported
Bits 22-20: Reserved
Bits 19-10: Operating Current/Power (units depend on PDO type)
Bits 9-0:   Max/Min Operating Current/Power
```

### PDO/RDO Array Handling

**Source Capabilities format:**
```
Byte 0:      PDO Count (0-7)
Bytes 1-4:   PDO 1 (32 bits, little-endian)
Bytes 5-8:   PDO 2
...
Bytes 25-28: PDO 7
```

**Parsing:**
```cpp
uint8_t pdoCount = data[0];
if (pdoCount > 7) {
    return false;  // Invalid
}

for (uint8_t i = 0; i < pdoCount; i++) {
    uint32_t pdo =
        data[1 + i*4 + 0] |
        (data[1 + i*4 + 1] << 8) |
        (data[1 + i*4 + 2] << 16) |
        (data[1 + i*4 + 3] << 24);

    parsePDO(pdo, i);
}
```

### USB PD Specification Reference

For complete details, refer to:
- **USB Power Delivery Specification 3.1**
- Section 6.4.1: Power Data Objects
- Table 6-7: Fixed Supply PDO
- Table 6-9: Battery Supply PDO
- Table 6-11: Variable Supply PDO
- Table 6-13: Augmented Power Data Object

---

## Memory Considerations

### RAM Constraints

**Teensy 4.0:** 512 KB RAM
**Teensy 4.1:** 1024 KB RAM

**Guidelines:**
- Minimize global variables
- Use `const` and `constexpr` for constants (stored in flash)
- Use `F()` macro for string literals
- Avoid large stack allocations

### Flash Memory for Strings

**❌ Waste RAM:**
```cpp
Serial.println("This string uses RAM");
```

**✅ Save RAM:**
```cpp
Serial.println(F("This string is in flash"));
```

**For const data:**
```cpp
// Stored in flash
const char* CONNECTION_STATES[] PROGMEM = {
    "Not Connected",
    "Connected.DFP",
    "Connected.UFP",
    // ...
};
```

### Stack vs Heap

**Prefer stack allocation for small objects:**
```cpp
TPS25751Status status(data, 5);  // Stack - fast, automatic cleanup
```

**Use heap for large or dynamic objects:**
```cpp
auto reg = std::make_unique<TPS25751Status>();  // Heap - managed lifetime
```

**Avoid:**
```cpp
uint8_t largeBuffer[1024];  // Too large for stack on embedded!
```

---

## Common Pitfalls

### 1. Forgetting I2C Length Byte

**Wrong:**
```cpp
Wire.requestFrom(addr, 5);
data[0] = Wire.read();  // This is LENGTH, not data!
```

**Right:**
```cpp
Wire.requestFrom(addr, 1);
uint8_t length = Wire.read();
Wire.requestFrom(addr, length);
// Now read actual data
```

### 2. Using dynamic_cast

**Wrong:**
```cpp
auto* status = dynamic_cast<TPS25751Status*>(reg.get());  // Won't compile!
```

**Right:**
```cpp
auto* status = static_cast<TPS25751Status*>(reg.get());
```

### 3. String Literals in RAM

**Wrong:**
```cpp
for (int i = 0; i < 100; i++) {
    Serial.println("Iteration");  // Wastes RAM
}
```

**Right:**
```cpp
for (int i = 0; i < 100; i++) {
    Serial.println(F("Iteration"));  // Stored in flash
}
```

### 4. Incorrect PDO Unit Conversion

**Wrong:**
```cpp
// All PDO types use same units - WRONG!
uint16_t voltage = (pdo >> 10) & 0x3FF;
uint16_t voltageMv = voltage * 50;  // Wrong for PPS!
```

**Right:**
```cpp
uint8_t type = (pdo >> 30) & 0x03;
if (type == 0b11) {  // PPS uses 100mV units
    uint16_t voltage = (pdo >> 17) & 0x1FF;
    uint16_t voltageMv = voltage * 100;
} else {  // Others use 50mV units
    uint16_t voltage = (pdo >> 10) & 0x3FF;
    uint16_t voltageMv = voltage * 50;
}
```

### 5. Not Checking Reserved Bits

**Wrong:**
```cpp
bool validateData() const {
    return true;  // Too permissive!
}
```

**Right:**
```cpp
bool validateData() const {
    // Check reserved bits are 0
    if ((data_[0] & RESERVED_MASK) != 0) {
        return false;
    }
    return true;
}
```

### 6. Large Stack Allocations

**Wrong:**
```cpp
void processData() {
    uint8_t buffer[2048];  // Too large for embedded stack!
}
```

**Right:**
```cpp
void processData() {
    static uint8_t buffer[2048];  // Static/global, or
    auto buffer = std::make_unique<uint8_t[]>(2048);  // Heap
}
```

---

## Platform-Specific Workarounds

### Dealing with RTTI Absence

**Problem:** Need type identification without RTTI

**Solution:** Use factory pattern with address-based lookup

```cpp
class TPS25751Register {
public:
    virtual uint8_t getAddress() const = 0;

    // Type-safe casting helper — compare against the expected register's address
    template<typename T>
    T* as() {
        if (getAddress() == static_cast<uint8_t>(T::staticAddress())) {
            return static_cast<T*>(this);
        }
        return nullptr;
    }
};
```

### Template Metaprogramming Without static_assert(false)

**Problem:** Need compile-time errors for invalid template instantiations

**Solution:** Use dependent types

```cpp
template<typename>
struct always_false : std::false_type {};

template<typename T>
void foo() {
    static_assert(always_false<T>::value, "Invalid type");
}
```

---

## Reference Documents

1. **TI TPS25751 Technical Reference Manual**
   - I2C protocol details
   - Register definitions
   - Electrical characteristics

2. **USB Power Delivery Specification 3.1**
   - PDO/RDO formats
   - Negotiation protocols
   - Voltage/current encoding

3. **Teensy 4.x Documentation**
   - Memory layout
   - I2C implementation
   - Arduino compatibility

4. **ARM Cortex-M7 Technical Reference**
   - Processor capabilities
   - Optimization guidelines

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial constraints document extracted from implementation plan |

---

*Understanding these constraints is essential for successful implementation on the target platform.*
