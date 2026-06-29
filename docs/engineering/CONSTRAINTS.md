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

### Writing Registers

Writes mirror the read framing: the length byte is **sent**, not just received.

**TPS25751 I2C write:**
```
START | ADDR+W | REG | LENGTH | DATA... | STOP
                       ^^^^^^^
                       Byte count, sent right after the register address
```

**Correct implementation** (`TPS25751::writeRegister`, `src/TPS25751.cpp`):
```cpp
bool TPS25751::writeRegister(uint8_t regAddr, const uint8_t* data, size_t length) const {
    _wire.beginTransmission(_address);
    _wire.write(regAddr);
    _wire.write(static_cast<uint8_t>(length));   // length byte, same as the read path
    size_t written = _wire.write(data, length);
    if (written != length) return false;          // didn't queue all bytes
    return _wire.endTransmission(true) == 0;
}
```

**Common mistake:**
```cpp
// WRONG - omits the length byte; the controller will misinterpret the payload
Wire.beginTransmission(addr);
Wire.write(regAddr);
Wire.write(data, length);
Wire.endTransmission();
```

### 4CC Command Interface & I2Cc Downstream-Device Proxy (TRM Sec 4.4.2/4.4.3)

Beyond the 15 directly-mapped status/config registers, the TPS25751 exposes a generic
**4CC command-task** interface through two registers:

- **COMMAND** (`0x08`, 4 bytes): write a 4-character ASCII task code (e.g. `"I2Cr"`) to
  start a task. The controller **clears COMMAND to all-zero** on success, or **writes
  back `"!CMD"`** if the code is unrecognized. Reading COMMAND while a task is in
  flight returns the in-progress code.
- **DATA** (`0x09`, 64 bytes): generic scratch register carrying the task's
  input/output payload. It has no fixed layout of its own — interpretation depends
  entirely on which task is running.

The handshake (`TPS25751::executeCommand()`, `src/TPS25751.cpp`): write input payload to
DATA (if any) → write the 4CC to COMMAND → poll COMMAND until it clears or rejects (or
`timeoutMs` elapses) → on success, read the full 64-byte DATA back (`DATA[0]` is the
task's return code).

**I2Cr** and **I2Cw** are 4CC tasks that proxy register access to a device on the
TPS25751's *secondary* I2C bus (I2Cc_SDA/SCL), e.g. a BQ25798 charger
(`TPS25751DownstreamDevice`, `include/TPS25751DownstreamDevice.h`):

| Task | Input DATA layout | Output DATA layout |
|------|--------------------|---------------------|
| **I2Cr** (read) | `DATA[0]`=target addr (7-bit, bit 7 reserved=0), `DATA[1]`=register offset, `DATA[2]`=NumBytes (max **63** — `DATA[0]` holds the return code) | `DATA[0]`=task return code, `DATA[1..NumBytes]`=bytes read — **read data starts at offset 1**, not 0 |
| **I2Cw** (write) | `DATA[0]`=target addr (7-bit, bit 7 reserved=0), `DATA[1]`=**Length**, `DATA[2]`=register offset, `DATA[3..]`=payload (max 11 bytes). **`Length` counts the register-offset byte + payload (= payload_len + 1)**: the controller transmits exactly `Length` bytes starting at the offset byte, so `Length == payload_len` does a pointer-only write that changes nothing. | `DATA[0]`=task return code (success only means the write was *queued*, not necessarily completed on the I2Cc wire — confirm with an I2Cr read-back) |

> **I2Cr vs I2Cw are not symmetric.** I2Cr is `{addr, regOffset, numBytes}` with the count = data bytes. I2Cw is `{addr, Length, regOffset, payload}` — length and offset are swapped **and** `Length` includes the offset byte (`payload_len + 1`). Both confirmed on hardware against a BQ25798.

**5-second minimum spacing:** per the TRM, consecutive **I2Cr** commands — and,
separately, consecutive **I2Cw** commands — must be issued **at least 5 seconds
apart**. This is host pacing guidance distinct from `executeCommand()`'s much
shorter (~200 ms) completion poll. `TPS25751DownstreamDevice` tracks the last
issue time of each task type via `millis()` and emits a `DEBUG_CAT_TASK` warning
if called sooner — it does **not** hard-block, since callers may already be
managing the timing themselves.

> **Direct (non-proxied) transport bypasses all of the above.** Constructing
> `TPS25751DownstreamDevice` / `BQ25798::Device` with the `(TwoWire&, addr)`
> constructor instead of `(const TPS25751&, addr)` selects a **direct** transport:
> reads/writes are plain Arduino-`Wire` register transactions to the part on the
> MCU's own bus, with no 4CC relay. Because there is no I2Cr/I2Cw, **none** of the
> proxy constraints apply — no TRM 5 s spacing, no 63-byte read / 11-byte write
> payload cap (the only limit is the `TwoWire` buffer), and the I2Cr-vs-I2Cw
> asymmetry is irrelevant. The trade-off: direct mode requires the part to actually
> be wired to the bus you pass (it is *not* on the TPS25751's I2Cc bus), and the
> caller must `wire.begin()` it. Use it for bench/bring-up; use the proxied
> constructor for the production I2Cc topology. See ADR-010 in ARCHITECTURE.md.

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

### 7. Forgetting the I2Cr/I2Cw Target Address Is 7-Bit

**Wrong:**
```cpp
// WRONG - passes a raw 8-bit address (possibly including a R/W bit) straight
// into the I2Cr/I2Cw input payload
uint8_t in[3] = { deviceAddress, devReg, len };
```

**Right:**
```cpp
// RIGHT - bit 7 of the I2Cr/I2Cw target-address byte is reserved (must be 0);
// mask it off once, e.g. in the TPS25751DownstreamDevice constructor
_deviceAddress = static_cast<uint8_t>(deviceAddress & 0x7F);
```

Also remember the I2Cr **read data starts at DATA offset 1** (`DATA[0]` is the task
return code), the I2Cw payload is capped at **11 bytes**, and the I2Cw **`Length`
field counts the register-offset byte + payload (`payload_len + 1`)** — see
[4CC Command Interface & I2Cc Downstream-Device Proxy](#4cc-command-interface--i2cc-downstream-device-proxy-trm-sec-442443).

---

## BQ25798 Downstream Device Decode & Encode Conventions

These apply to all BQ25798 register classes and, by analogy, to any downstream
device whose registers are big-endian. Conventions 1–3 cover **decode** (read);
conventions 4–6 cover **encode** (write) of the R/W registers (see ADR-009 in
[ARCHITECTURE.md](ARCHITECTURE.md)).

### 1. 16-Bit Registers Are Big-Endian — Do Not Use `extractBits16`

`TPS25751BitUtils::extractBits16` assembles `raw[0]` as the **LSB** (little-endian).
BQ25798 stores 16-bit registers with `raw[0]` as the **MSB** (big-endian). Using
`extractBits16` silently byte-swaps every 16-bit value.

**Wrong:**
```cpp
uint16_t v = TPS25751BitUtils::extractBits16(data_.data(), 0, 16, 0); // byte-swapped!
```

**Right:**
```cpp
uint16_t v = (static_cast<uint16_t>(data_[0]) << 8) | data_[1];
```

### 2. Signed ADC Channels — Reinterpret as `int16_t`

The BQ25798 IBUS, IBAT, and TDIE ADC registers return 2's-complement signed
values. After big-endian assembly, reinterpret as `int16_t`:

**Wrong:**
```cpp
uint16_t raw = (static_cast<uint16_t>(data_[0]) << 8) | data_[1];
float ibus_mA = raw * 1.0f;   // wrong sign for negative currents
```

**Right:**
```cpp
uint16_t raw = (static_cast<uint16_t>(data_[0]) << 8) | data_[1];
int16_t signed_raw = static_cast<int16_t>(raw);
float ibus_mA = signed_raw * lsb_mA;  // correct sign
```

### 3. ADC LSB Step Sizes Come from the Datasheet, Not the MCP Definitions

The `bq25798-docs` MCP server provides register field descriptions but does **not**
carry unit-conversion factors (ADC LSB step sizes). Source these from the BQ25798
datasheet (SLUSE02, Table 9-xx for each ADC channel):

| Channel | LSB | Notes |
|---------|-----|-------|
| IBUS | 1 mA | Signed |
| IBAT | 1 mA | Signed |
| VBUS | 1 mV | Unsigned |
| VAC1/VAC2 | 1 mV | Unsigned |
| VBAT | 1 mV | Unsigned |
| VSYS | 1 mV | Unsigned |
| TS | 0.09765625 % | Unsigned (NTC ratio) |
| TDIE | 0.5 °C | Signed |

Hardcoding the wrong LSB (e.g. 1.0 for TDIE) produces silently wrong physical values.

### 4. Encode Is Read-Modify-Write — Preserve Reserved Bits

Field setters on R/W registers mutate **only their field's bits** in the register's own
raw buffer (`TPS25751Register::raw()`); reserved and sibling-field bits are preserved by
construction. Never reassemble the whole register from a subset of fields (that risks
zeroing reserved bits the device expects to read back unchanged). The encode helpers
`setField8` / `setField16BE` (`include/BQ25798/BQ25798Encode.h`) do this read-modify-write
and mask the value to the field width.

### 5. Field Placement Is Big-Endian on Encode Too — Use `setField16BE`

The encode side mirrors convention 1: for 16-bit registers, `setField16BE` writes the
high byte to `raw[0]` (big-endian), matching the decode-side `(raw[0]<<8)|raw[1]`
assembly. Never use a little-endian helper to encode a downstream 16-bit field — it
byte-swaps the value just as `extractBits16` does on decode.

### 6. Inverse Unit Conversion Reuses the Class's Own Decode Constant

An engineering-unit setter (e.g. `setMillivolts`) inverts the same LSB/offset constant
its decode accessor uses, so encode and decode can never drift apart. Where decode adds
an offset, the inverse must **guard against unsigned underflow** (clamp inputs below the
offset to 0 before subtracting):

```cpp
// decode: mV = kOffsetMv + code * kLsbMv
// encode (with underflow guard):
const uint16_t code = (mV <= kOffsetMv) ? 0
                      : static_cast<uint16_t>((mV - kOffsetMv) / kLsbMv);
```

### Encode Reuses the Existing I2Cw Framing

Typed writes (`Device::writeRegister<T>()`) ride the same I2Cw 4CC task as the raw write
path, so the framing constraints from the [4CC Command Interface](#4cc-command-interface--i2cc-downstream-device-proxy-trm-sec-442443)
section apply unchanged: payload `len ≤ 11`, and `Length = payload + 1` (counts the
register-offset byte). Self-clearing command bits (REG_RST, WD_RST, FORCE_*) are written
as a `setX(true)`; the device clears them in hardware, so a subsequent read returns 0.

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
| 1.1 | 2026-06-24 | Claude Code | Added BQ25798 downstream device decode conventions (big-endian, signed ADC, LSB sources) |

---

*Understanding these constraints is essential for successful implementation on the target platform.*
