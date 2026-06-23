# TPS25751 Library - System Architecture

**Last Updated:** 2025-10-20
**Status:** Active

---

## Overview

This document describes the system architecture and design patterns of the TPS25751 library. Understanding this architecture is essential for implementing new features, debugging issues, and maintaining consistency across the codebase.

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Component Architecture](#component-architecture)
3. [Design Patterns](#design-patterns)
4. [Class Hierarchy](#class-hierarchy)
5. [Data Flow](#data-flow)
6. [Extension Points](#extension-points)

---

## System Overview

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
│                  (User Sketches / Examples)                  │
└───────────────────────────────┬─────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────┐
│                    TPS25751 Main Class                       │
│              (Convenience Methods / I2C Manager)             │
└───────────────────────────────┬─────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
┌───────▼────────┐    ┌────────▼─────────┐    ┌───────▼────────┐
│  Factory Layer │    │  Register Layer  │    │  I2C Protocol  │
│                │    │                  │    │                │
│ - Create regs  │    │ - Validation     │    │ - Length byte  │
│ - Type mapping │    │ - Parsing        │    │ - Wire lib     │
│ - Registry     │    │ - Debug print    │    │ - Error handle │
└────────┬───────┘    └────────┬─────────┘    └────────┬───────┘
         │                     │                       │
         └─────────────────────┼───────────────────────┘
                               │
                    ┌──────────▼──────────┐
                    │   Utility Layer     │
                    │                     │
                    │ - Bit extraction    │
                    │ - Debug helpers     │
                    │ - Type traits       │
                    └─────────────────────┘
```

### Layer Responsibilities

**Application Layer:**
- User code / example sketches
- High-level USB-C PD logic
- Application-specific behavior

**Main Class (TPS25751):**
- I2C communication management
- Register read/write coordination
- Convenience methods for common operations
- Error handling and retry logic

**Factory Layer:**
- Register object creation
- Type-to-address mapping
- Dependency injection support
- Singleton pattern for global access

**Register Layer:**
- Individual register implementations
- Data validation (3 tiers)
- Bitfield parsing
- Debug output formatting

**I2C Protocol Layer:**
- TPS25751-specific protocol handling
- Length-prefixed reads and writes
- Wire library abstraction
- Communication error handling

**4CC Command-Task Layer:**
- Generic command-task executor (`TPS25751::executeCommand()`) over the COMMAND
  (0x08) / DATA (0x09) registers (TRM Sec 4.4.2)
- `TPS25751Command` / `TPS25751Data` register classes (COMMAND/DATA decoders)
- `TPS25751Task.h` value types: `TPS25751FourCC`, `TPS25751TaskStatus`,
  `TPS25751TaskResult`
- DATA write → COMMAND write → poll-to-clear/reject → DATA read handshake

**I2Cc Downstream-Proxy Layer:**
- `TPS25751DownstreamDevice` — proxies register access to a device on the
  TPS25751's secondary I2C bus (I2Cc_SDA/SCL) via the I2Cr/I2Cw 4CC tasks
- Encodes/decodes the I2Cr/I2Cw DATA payloads (target address, register offset,
  length, payload)
- Future downstream device register classes (e.g. a BQ25798 charger) reuse the
  same `TPS25751Register` decoder base as the host-side registers

**Utility Layer:**
- Bit manipulation helpers
- Debug infrastructure
- Common type definitions
- Platform-specific utilities

---

## Component Architecture

### Core Components

#### 1. TPS25751 Main Controller

**File:** `lib/TPS25751/src/TPS25751.cpp`

**Purpose:** Central coordination of I2C communication and register access

**Key Methods:**
```cpp
class TPS25751 {
public:
    bool begin(uint8_t address = 0x20);

    // Register read convenience methods
    std::unique_ptr<TPS25751Status> readStatusRegister(bool validate = false);
    std::unique_ptr<TPS25751Mode> readModeRegister(bool validate = false);
    // ... one per register type

    // Generic register access
    bool readRegister(uint8_t regAddr, uint8_t* buffer, size_t bufferSize);
    bool writeRegister(uint8_t regAddr, const uint8_t* data, size_t length);

private:
    uint8_t i2cAddress_;
    TwoWire* wire_;  // I2C interface (Wire or Wire1)
};
```

**Responsibilities:**
- Initialize I2C communication
- Manage device address
- Provide type-safe register access
- Handle I2C protocol details
- Error detection and reporting

#### 2. TPS25751Register Base Class

**File:** `lib/TPS25751/src/TPS25751Register.cpp`

**Purpose:** Abstract base for all register implementations

**Class Hierarchy:**
```cpp
class TPS25751Register {
public:
    // Pure virtual - must override
    virtual uint8_t getAddress() const = 0;
    virtual uint8_t getExpectedSize() const = 0;
    virtual bool isReadOnly() const = 0;
    virtual bool validateBasic() const = 0;
    virtual bool validateData() const = 0;
    virtual bool validateSemantic() const = 0;
    virtual void debugPrint() const = 0;

    // Implemented in base
    uint8_t getDataLength() const;
    const uint8_t* getData() const;

    // Rule of Five
    virtual ~TPS25751Register() = default;
    TPS25751Register(const TPS25751Register& other);
    TPS25751Register(TPS25751Register&& other) noexcept;
    TPS25751Register& operator=(const TPS25751Register& other);
    TPS25751Register& operator=(TPS25751Register&& other) noexcept;

protected:
    TPS25751Register(uint8_t address, uint8_t expectedSize);
    TPS25751Register(uint8_t address, uint8_t expectedSize,
                     const uint8_t* data, size_t length);

    // Data storage
    std::vector<uint8_t> data_;
    uint8_t address_;
    uint8_t expectedSize_;
};
```

**Responsibilities:**
- Define register interface contract
- Manage raw register data storage
- Provide RAII memory management
- Enable polymorphic register handling

#### 3. Concrete Register Classes

**Example:** `TPS25751Status`

```cpp
class TPS25751Status : public TPS25751Register {
public:
    TPS25751Status();
    TPS25751Status(const uint8_t* data, size_t length);

    // Accessor methods for each field
    uint8_t getConnectionState() const;
    const char* getConnectionStateString() const;
    bool isVBUSPresent() const;
    uint8_t getPowerRole() const;
    // ... more accessors

    // Override base class pure virtuals
    uint8_t getAddress() const override { return 0x1A; }
    uint8_t getExpectedSize() const override { return 5; }
    bool isReadOnly() const override { return true; }
    bool validateBasic() const override;
    bool validateData() const override;
    bool validateSemantic() const override;
    void debugPrint() const override;

private:
    // Helper methods
    uint8_t extractConnectionState() const;
    uint8_t extractPowerRole() const;
    // ... more helpers
};
```

**Current Implementations:** (9 total)
- TPS25751Status (0x1A)
- TPS25751Mode (0x03)
- TPS25751PowerPathStatus (0x26)
- TPS25751BootFlags (0x2D)
- TPS25751PortConfig (0x28)
- TPS25751TypeCState (0x69)
- TPS25751InterruptEvent (0x14)
- TPS25751PowerStatus (0x3F)
- TPS25751PDStatus (0x40)

#### 4. Factory Pattern Implementation

**Files:**
- `lib/TPS25751/include/TPS25751RegisterFactory.h`
- `lib/TPS25751/src/TPS25751RegisterFactory.cpp`

**Components:**

**A. Register Identity — `TPS25751Registers::Address`:**

Registers are identified by their hardware address. There is no separate factory
type enum; `TPS25751Registers::Address` (in `TPS25751RegisterAddress.h`) is the
single source of truth and the key the factory dispatches on.

**B. Abstract Factory Interface:**
```cpp
class TPS25751RegisterFactory {
public:
    virtual ~TPS25751RegisterFactory() = default;

    virtual std::unique_ptr<TPS25751Register>
        createRegister(TPS25751Registers::Address addr) = 0;

    virtual std::unique_ptr<TPS25751Register>
        createRegister(TPS25751Registers::Address addr, const uint8_t* data, size_t length) = 0;

    virtual std::unique_ptr<TPS25751Register>
        createRegister(const TPS25751Registers::RegisterInfo& regInfo) = 0;
};
```

An address without a decoder class returns `nullptr` from `createRegister()` — that
is how un-decoded registers are reported (previously the `RegisterType::UNKNOWN`
sentinel).

**C. Concrete Factory:**
```cpp
class TPS25751RegisterFactoryImpl : public TPS25751RegisterFactory {
public:
    std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr) override;
    // ... other overrides

private:
    std::unique_ptr<TPS25751Register> createStatusRegister() const;
    std::unique_ptr<TPS25751Register> createModeRegister() const;
    // ... one per register type
};
```

**D. Singleton Access:**
```cpp
class TPS25751Factory {
public:
    static TPS25751RegisterFactory& getInstance();
    static void setFactory(std::unique_ptr<TPS25751RegisterFactory> factory);

private:
    static std::unique_ptr<TPS25751RegisterFactory> instance_;
};
```

#### 5. Utility Components

**TPS25751BitUtils:**
```cpp
class TPS25751BitUtils {
public:
    static uint8_t extractBits(const uint8_t* data, uint8_t byteIndex,
                               uint8_t bitCount, uint8_t bitOffset);

    static uint16_t extractBits16(const uint8_t* data, uint8_t startByte,
                                  uint8_t bitCount, uint8_t bitOffset);

    static uint32_t extractBits32(const uint8_t* data, uint8_t startByte,
                                  uint8_t bitCount, uint8_t bitOffset);

    static bool getBit(const uint8_t* data, uint8_t byteIndex, uint8_t bitIndex);

    static void setBit(uint8_t* data, uint8_t byteIndex, uint8_t bitIndex, bool value);
};
```

**TPS25751Debug:**
```cpp
// Debug categories
#define DEBUG_CAT_I2C        0x01
#define DEBUG_CAT_REGISTER   0x02
#define DEBUG_CAT_VALIDATION 0x04
#define DEBUG_CAT_FACTORY    0x08

// Debug levels
#define DEBUG_LEVEL_NONE     0
#define DEBUG_LEVEL_ERROR    1
#define DEBUG_LEVEL_WARNING  2
#define DEBUG_LEVEL_INFO     3
#define DEBUG_LEVEL_VERBOSE  4

class TPS25751Debug {
public:
    static void print(uint8_t category, uint8_t level, const char* message);
    static void setLevel(uint8_t level);
    static void enableCategory(uint8_t category);
    static void disableCategory(uint8_t category);
};
```

---

## Design Patterns

### 1. Factory Method Pattern

**Intent:** Centralize object creation and type management without RTTI

**Why:** Platform compiles with `-fno-rtti`, cannot use `dynamic_cast` or `typeid`

**Implementation:**
```cpp
// Create by address (the single register identity)
auto reg = TPS25751Factory::getInstance()
    .createRegister(TPS25751Registers::Address::STATUS);

// Type-safe casting — dispatch on the register's address
if (reg && reg->getAddress() == static_cast<uint8_t>(TPS25751Registers::Address::STATUS)) {
    auto* statusPtr = static_cast<TPS25751Status*>(reg.get());
}
```

**Benefits:**
- Type safety without RTTI
- Centralized creation logic
- Easy to add new register types
- Testable (can inject mock factory)

### 2. Template Method Pattern

**Intent:** Define validation skeleton, let subclasses implement steps

**Implementation:**
```cpp
// Base class defines the template
class TPS25751Register {
public:
    bool validate() const {
        return validateBasic() && validateData() && validateSemantic();
    }

    // Steps implemented by subclasses
    virtual bool validateBasic() const = 0;
    virtual bool validateData() const = 0;
    virtual bool validateSemantic() const = 0;
};

// Subclass implements steps
class TPS25751Status : public TPS25751Register {
    bool validateBasic() const override {
        return getDataLength() == getExpectedSize();
    }

    bool validateData() const override {
        return checkReservedBits() && checkFieldRanges();
    }

    bool validateSemantic() const override {
        return checkLogicalConsistency();
    }
};
```

**Benefits:**
- Consistent validation flow
- Each level independently testable
- Performance flexibility (choose validation depth)

### 3. Singleton Pattern

**Intent:** Single global factory instance with injection capability

**Implementation:**
```cpp
class TPS25751Factory {
public:
    static TPS25751RegisterFactory& getInstance() {
        if (!instance_) {
            instance_ = std::make_unique<TPS25751RegisterFactoryImpl>();
        }
        return *instance_;
    }

    static void setFactory(std::unique_ptr<TPS25751RegisterFactory> factory) {
        instance_ = std::move(factory);
    }

private:
    static std::unique_ptr<TPS25751RegisterFactory> instance_;
};
```

**Benefits:**
- Global access point
- Lazy initialization
- Testable (can inject mock factory)
- Thread-safe on embedded (single-threaded)

### 4. RAII (Resource Acquisition Is Initialization)

**Intent:** Automatic resource management

**Implementation:**
```cpp
class TPS25751Register {
public:
    TPS25751Register(uint8_t address, uint8_t expectedSize,
                     const uint8_t* data, size_t length)
        : address_(address)
        , expectedSize_(expectedSize)
        , data_(data, data + length)  // Copy into vector
    {
        // Resource acquired in constructor
    }

    ~TPS25751Register() {
        // Automatic cleanup (vector destructor)
    }

private:
    std::vector<uint8_t> data_;  // RAII container
};
```

**Benefits:**
- No manual memory management
- Exception-safe (if exceptions enabled)
- Automatic cleanup
- No memory leaks

### 5. Strategy Pattern (Validation)

**Intent:** Select validation strategy at runtime

**Implementation:**
```cpp
// Different validation strategies
enum class ValidationLevel {
    BASIC,      // Fast, minimal checks
    DATA,       // Medium, field validation
    SEMANTIC    // Slow, full logical checks
};

class TPS25751Register {
public:
    bool validate(ValidationLevel level) const {
        switch (level) {
            case ValidationLevel::BASIC:
                return validateBasic();
            case ValidationLevel::DATA:
                return validateBasic() && validateData();
            case ValidationLevel::SEMANTIC:
                return validateBasic() && validateData() && validateSemantic();
        }
    }
};
```

**Benefits:**
- Performance tuning
- Different validation in production vs. debug
- Progressive validation

---

## Class Hierarchy

### Inheritance Tree

```
TPS25751Register (abstract base)
├── TPS25751Status
├── TPS25751Mode
├── TPS25751PowerPathStatus
├── TPS25751BootFlags
├── TPS25751PortConfig
├── TPS25751TypeCState
├── TPS25751InterruptEvent
├── TPS25751PowerStatus
├── TPS25751PDStatus
├── TPS25751Command           (COMMAND, 0x08 — 4CC command-task control)
├── TPS25751Data              (DATA, 0x09 — generic 4CC task payload)
└── ... (downstream device register classes, e.g. a future BQ25798ChargerStatus,
        extend this same base — see TPS25751DownstreamDevice::readRegister<T>())

TPS25751RegisterFactory (abstract factory)
└── TPS25751RegisterFactoryImpl (concrete factory)

TPS25751Factory (singleton)

TPS25751 (main controller)
└── executeCommand() — 4CC command-task executor (COMMAND/DATA handshake)

TPS25751DownstreamDevice (I2Cc proxy; composes a TPS25751 host, not a subclass)

TPS25751BitUtils (utility, all static)

TPS25751Debug (utility, all static)
```

**Note:** downstream-device register classes (for parts on the TPS25751's I2Cc bus,
e.g. a BQ25798 charger) are **not** TPS25751-specific — they extend the same
device-agnostic `TPS25751Register` decoder base used above, and are read via
`TPS25751DownstreamDevice::readRegister<T>(devReg, out, len)` rather than the
host's own factory.

### Relationship Diagram

```
┌────────────────┐
│   TPS25751     │
│                │
│ Main Controller│
└───────┬────────┘
        │ uses
        ▼
┌────────────────────┐      ┌──────────────────┐
│ TPS25751Factory    │      │ TPS25751Register │
│                    │      │  (abstract base) │
│ Singleton          │◄─────┤                  │
└─────────┬──────────┘      └────────▲─────────┘
          │ manages                   │
          ▼                           │ inherits
┌──────────────────────┐              │
│ TPS25751Register     │              │
│ FactoryImpl          │        ┌─────┴──────────┐
│                      │        │                │
│ creates───────────────────────►│ TPS25751Status │
└──────────────────────┘        │ TPS25751Mode   │
                                │ ...            │
                                └────────────────┘
```

---

## Data Flow

### Register Read Flow

```
1. Application calls TPS25751::readStatusRegister()
        │
        ▼
2. TPS25751 calls readRegister(0x1A, buffer, 5)
        │
        ▼
3. I2C Protocol Layer:
   - Wire.beginTransmission(address)
   - Wire.write(0x1A)  // Register address
   - Wire.endTransmission(false)  // Repeated start
   - Wire.requestFrom(address, 1)  // Read length byte
   - length = Wire.read()
   - Wire.requestFrom(address, length)  // Read data
   - Read 'length' bytes into buffer
        │
        ▼
4. Factory creates register object:
   - factory.createRegister(TPS25751Registers::Address::STATUS, buffer, length)
   - Returns std::unique_ptr<TPS25751Status>
        │
        ▼
5. Application validates if needed:
   - status->validateData()
        │
        ▼
6. Application accesses data:
   - status->getConnectionState()
   - status->isVBUSPresent()
        │
        ▼
7. Bitfield extraction (internal):
   - TPS25751BitUtils::extractBits(data, byte, count, offset)
        │
        ▼
8. Return value to application
```

### Register Write Flow

The write path mirrors the read path's length-prefixed framing, but **sends** the
length byte instead of receiving it. Implemented as `TPS25751::writeRegister()`
(private raw overload + two public type-safe overloads, `include/TPS25751.h`,
`src/TPS25751.cpp`):

```
1. Application calls a type-safe overload:
   - tps.writeRegister(TPS25751Registers::Address::COMMAND, data, length)
   - tps.writeRegister(TPS25751Registers::RegisterInfo, data)
        │
        ▼
2. Size guard (Address overload only):
   - getRegisterSize(addr) must equal `length` (0 = unknown address, skip check)
        │
        ▼
3. Private raw writeRegister(regAddr, data, length):
   - Wire.beginTransmission(address)
   - Wire.write(regAddr)
   - Wire.write((uint8_t)length)        // length byte, sent (not received)
   - Wire.write(data, length)            // verify return == length
   - Wire.endTransmission(true)          // verify return == 0
        │
        ▼
4. TPS_REPORT_I2C_ERROR / TPS_DEBUG_* on any failure; bool success returned
```

### 4CC Command-Task Flow (`executeCommand`, TRM Sec 4.4.2)

```
1. Application calls:
   - tps.executeCommand(TPS25751FourCC::of("I2Cr"), inData, inLen, outData, outLen)
        │
        ▼
2. If inData: write it to DATA via the private raw writeRegister(uint8_t, data, inLen)
   - (NOT the public Address overload — that requires length == 64 for DATA and
     would reject a short payload)
        │
        ▼
3. Write the 4CC to COMMAND:
   - writeRegister(Address::COMMAND, cmd.code, 4)
        │
        ▼
4. waitForCommandClear(timeoutMs) polls COMMAND (~5 ms between reads):
   - TPS25751Command(buf,4).isClear()    → Success
   - TPS25751Command(buf,4).isRejected() → Rejected ("!CMD")
   - elapsed > timeoutMs                 → Timeout
   - I2C read failure                    → I2CError
        │
        ▼
5. On Success, read the full 64-byte DATA register back (private raw readRegister)
        │
        ▼
6. returnCode = DATA[0]; if outData, memcpy(outData, DATA, min(outLen, 64))
        │
        ▼
7. Return TPS25751TaskResult{status, returnCode}
```

This handshake is the basis for `TPS25751DownstreamDevice`'s I2Cr/I2Cw codec (see
[I2Cc Downstream-Proxy Layer](#layer-responsibilities) and CONSTRAINTS.md's
"4CC Command Interface & I2Cc Downstream-Device Proxy" section) — `readRegister()`/
`writeRegister()` there call `executeCommand()` with the I2Cr/I2Cw input payload and
unpack the task-specific output layout (e.g. I2Cr's read bytes starting at DATA
offset 1) themselves, keeping `executeCommand()` itself task-agnostic.

```
Superseded sketch (kept for reference; not the real API
— there is no TPS25751::writeRegister(registerObject) overload):
1. Application creates/modifies register object:
   - TPS25751PortControl control;
   - control.setPortEnabled(true);
        │
        ▼
2. Application calls TPS25751::writeRegister(control)
        │
        ▼
3. Validate before write:
   - control.validateData()
        │
        ▼
4. I2C Protocol Layer:
   - Wire.beginTransmission(address)
   - Wire.write(registerAddress)
   - Wire.write(data, length)
   - Wire.endTransmission()
        │
        ▼
5. Verify write (optional):
   - Read back and compare
```

### Factory Creation Flow

```
1. Request: createRegister(TPS25751Registers::Address::STATUS)
        │
        ▼
2. Factory lookup: switch(addr)
        │
        ▼
3. Call type-specific creator:
   - createStatusRegister()
        │
        ▼
4. Construct object:
   - return std::make_unique<TPS25751Status>();
        │
        ▼
5. Return as base class pointer:
   - std::unique_ptr<TPS25751Register>
```

---

## Extension Points

### Adding a New Register Class

**Required Steps:**

1. **Create register class** inheriting from `TPS25751Register`
2. **Ensure the address exists** in `TPS25751Registers::Address` (+ `RegisterInfo` size)
3. **Add forward declaration** in factory header
4. **Implement factory methods** (create with/without data)
5. **Update factory switch statements** in all createRegister() variants (one `case` per address)
6. **Add convenience method** to TPS25751 main class (optional)
7. **Write unit tests**
8. **Document with Doxygen**

**Extension Points:**
- New register types via factory registration
- Custom validation logic via override
- Custom debug output via debugPrint()
- Additional accessor methods in subclass

### Adding a Downstream Device Driver

Devices on the TPS25751's secondary I2Cc bus (e.g. a BQ25798 charger) are **not**
added through the host's factory — they compose `TPS25751DownstreamDevice`
(`include/TPS25751DownstreamDevice.h`) instead:

1. **Construct** a `TPS25751DownstreamDevice` for the part's I2C address:
   `TPS25751DownstreamDevice charger(tps, 0x6B);`
2. **Define register classes** for the device exactly like a TPS25751 register —
   extend `TPS25751Register`, same three-tier-style validation
   (`isSemanticallyValid()`), same `debugPrint()` convention. No factory `case`
   is needed; these classes are device-specific and not part of
   `TPS25751RegisterFactory`.
3. **Read** via the typed overload: `device.readRegister<MyChargerStatus>(devReg, out, len)`
   (decodes through the I2Cr task; raw byte access via the non-template
   `readRegister(devReg, buf, len)` overload is also available).
4. **Write** via `device.writeRegister(devReg, data, len)` (I2Cw task, payload
   capped at 11 bytes per the TRM; the library handles the I2Cw `Length` field,
   which counts the register-offset byte + payload).
5. **Respect the TRM's 5-second minimum spacing** between consecutive I2Cr (or
   consecutive I2Cw) commands — `TPS25751DownstreamDevice` warns via
   `DEBUG_CAT_TASK` if violated but does not block the call.

### Dependency Injection Points

**Factory Injection:**
```cpp
// Replace global factory for testing
auto mockFactory = std::make_unique<MockRegisterFactory>();
TPS25751Factory::setFactory(std::move(mockFactory));

// Use mock factory
auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::STATUS);
// Returns mock object instead of real one
```

**I2C Injection:**
```cpp
// TPS25751 can use Wire or Wire1
TPS25751 tps(&Wire1);  // Use alternate I2C bus
```

### Customization Points

**Validation Levels:**
```cpp
class MyCustomRegister : public TPS25751Register {
    bool validateSemantic() const override {
        // Add application-specific validation
        return baseValidation() && myCustomChecks();
    }
};
```

**Debug Output:**
```cpp
class MyCustomRegister : public TPS25751Register {
    void debugPrint() const override {
        // Custom format for specific needs
        printInMyFormat();
    }
};
```

---

## Component Relationships

### Module Dependencies

```
Application
    ↓ uses
TPS25751 (main class)
    ↓ uses
    ├─→ TPS25751Factory (register creation)
    ├─→ TPS25751Register subclasses (data access)
    └─→ Wire (I2C communication)

TPS25751Factory
    ↓ creates
TPS25751Register subclasses
    ↓ uses
TPS25751BitUtils (bit extraction)
```

### Compile-Time Dependencies

```
TPS25751Register.h (base class)
    ↑ depends on
    │
    ├─ TPS25751Status.h
    ├─ TPS25751Mode.h
    └─ ... (all register headers)
        ↑ used by
        │
    TPS25751RegisterFactory.h
        ↑ used by
        │
    TPS25751.h (main class)
        ↑ used by
        │
    Application code
```

---

## Memory Management Strategy

### Ownership Model

**Factory creates, user owns:**
```cpp
// Factory creates with std::unique_ptr
auto status = factory.createRegister(TPS25751Registers::Address::STATUS);
// User owns, automatic cleanup when out of scope
```

**Main class convenience methods:**
```cpp
// TPS25751 creates and returns ownership
auto status = tps.readStatusRegister();
// Caller owns returned object
```

**Data storage:**
```cpp
class TPS25751Register {
private:
    std::vector<uint8_t> data_;  // Owned by register object
    // Automatic cleanup in destructor
};
```

### Move Semantics

**Prefer moving over copying:**
```cpp
// Factory returns by value (move)
std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr) {
    return std::make_unique<TPS25751Status>();  // Move, not copy
}

// Move constructor
TPS25751Status(TPS25751Status&& other) noexcept
    : TPS25751Register(std::move(other)) {
    // Move members
}
```

---

## Testing Architecture

### Unit Test Structure

```
Each register class has:
    - Constructor tests
    - Validation tests (3 levels)
    - Accessor tests (all public methods)
    - Edge case tests
    - Factory integration tests
```

### Integration Test Structure

```
Cross-component tests:
    - Factory creation for all types
    - Round-trip read/write
    - Multi-register sequences
    - Error handling chains
```

### Mock Points

**Mockable interfaces:**
- TPS25751RegisterFactory (abstract factory)
- I2C communication (can wrap Wire)
- Register classes (virtual methods)

---

## Future Architecture Considerations

### Planned Enhancements

1. **Caching Layer**
   - Cache frequently-read registers
   - Invalidate on write or interrupt
   - Reduce I2C traffic

2. **Async I2C**
   - Non-blocking register reads
   - Callback notification
   - Better performance

3. **Register Subscriptions**
   - Observer pattern
   - Notify on register changes
   - Event-driven architecture

4. **Configuration Profiles**
   - Save/restore device state
   - Profile management
   - Factory reset capability

5. **Interrupt-driven `executeCommand()` completion** *(deferred — see ADR-007)*
   - Optional `irqPin` on the `TPS25751` constructor
   - ISR sets a volatile flag on `cmd1Complete` (INT_EVENT1 bit 30) instead of
     busy-polling COMMAND
   - Executor waits on the flag, then reads INT_EVENT1 for the result +
     `i2cControllerNacked` (bit 82) and clears via INT_CLEAR1
   - `waitForCommandClear()` is already isolated as a single private helper in
     `TPS25751` specifically so this variant can replace it without touching the
     DATA staging / encode-decode logic

---

## Architectural Decision Records

### ADR-004: Factory Pattern for Register Creation
**Status:** Accepted
**Decision:** Use Factory Method pattern instead of direct instantiation
**Rationale:**
- Platform has no RTTI, need type management
- Centralized creation logic
- Testable via dependency injection
**Consequences:**
- All register creation goes through factory
- Type safety without dynamic_cast
- Slightly more boilerplate for new types

### ADR-005: Three-Tier Validation
**Status:** Accepted
**Decision:** Separate validation into Basic, Data, Semantic levels
**Rationale:**
- Performance tuning (choose validation depth)
- Progressive validation strategy
- Clear separation of concerns
**Consequences:**
- Every register implements 3 validation methods
- Validation can be skipped in production if needed
- Better test coverage

### ADR-006: std::vector for Data Storage
**Status:** Accepted
**Decision:** Use std::vector<uint8_t> instead of raw arrays
**Rationale:**
- Automatic memory management
- RAII compliance
- Safe resizing
**Consequences:**
- Small heap overhead
- No manual memory management
- Exception-safe (if enabled)

### ADR-007: 4CC Command-Task Layer + I2Cc Downstream-Device Proxy
**Status:** Accepted
**Date:** 2026-06
**Context:** The TPS25751 exposes most functionality as directly-addressed
registers, but reaches devices on its secondary I2Cc bus (e.g. a BQ25798
charger) only through a generic "4CC" command-task interface (COMMAND/DATA,
TRM Sec 4.4.2) — write a payload to DATA, write a 4-character task code to
COMMAND, poll for completion, read results back from DATA. The **I2Cr**/**I2Cw**
tasks are this interface's read/write proxy to the I2Cc bus.
**Decision:**
- Add a generic, task-agnostic executor (`TPS25751::executeCommand()`) on the
  host, plus `TPS25751Command`/`TPS25751Data` register classes wired into the
  existing factory like the other 15 registers.
- Add `TPS25751DownstreamDevice` as a *separate, composed* abstraction (not a
  `TPS25751Register` subclass, not part of `TPS25751RegisterFactory`) that
  encodes/decodes the I2Cr/I2Cw DATA payloads on top of `executeCommand()`.
- Downstream device register classes reuse the existing `TPS25751Register`
  decoder base, so they look and behave like host-side registers from the
  caller's perspective, without requiring a second factory.
- Completion detection is a simple ~200 ms busy-poll on COMMAND
  (`waitForCommandClear()`); an interrupt-driven variant is deferred (see
  Planned Enhancements) because the TRM's 5-second inter-command spacing is a
  separate constraint an IRQ does not eliminate — so async polling is a
  latency/CPU optimization, not a correctness fix, and isn't worth the
  ISR/INT_EVENT1/INT_CLEAR1 complexity until the I2Cc path is proven on
  hardware.
**Consequences:**
- The 4CC layer is generic — any future task code (not just I2Cr/I2Cw) can
  reuse `executeCommand()` directly.
- `TPS25751DownstreamDevice` must itself enforce the TRM's 5-second minimum
  spacing between consecutive same-type (I2Cr or I2Cw) commands; this is host
  pacing the executor cannot see, since it operates one task at a time.
- Adding a downstream device is cheaper than adding a TPS25751 register (no
  factory wiring), at the cost of not benefiting from the host's factory
  validation/dispatch machinery.

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial architecture document |
| 1.1 | 2026-06-22 | Claude Code | Added 4CC command-task layer, I2Cc downstream-device proxy (ADR-007), register write flow |

---

*Understanding this architecture ensures consistent, maintainable implementations.*
