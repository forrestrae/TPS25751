# TPS25751 Library - System Architecture

**Last Updated:** 2026-11-23
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
- Downstream device register classes reuse the same `TPS25751Register` decoder base
  as the host-side registers

**Downstream Device Driver Tier (external):**
- Typed per-device drivers build on the I2Cc downstream-proxy layer: each inherits
  `TPS25751DownstreamDevice`, keeps its own namespace/Address enum/factory, and reuses
  `TPS25751Register` as its decoder base (ADR-008/009)
- The reference implementation is the **BQ25798 buck-boost charger driver**
  (`BQ25798::Device`, 57 register classes), maintained in the sibling
  **`lib/BQ25798`** repository — it depends on this library, not vice versa

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

**Bit helpers (on `TPS25751Register`):** the bitfield helpers live on the register
base class, not in a separate utility class. Each has an instance form that reads the
register's own buffer and a static form that takes a buffer and its length:

```cpp
// Bit n of a register is bit n % 8 of byte n / 8; multi-byte fields are little-endian.
uint8_t  extractBits  (uint16_t bitPos, uint8_t bitLen) const;  // 1..8 bits
uint16_t extractBits16(uint16_t bitPos, uint8_t bitLen) const;  // 1..16 bits
uint32_t extractBits32(uint16_t bitPos, uint8_t bitLen) const;  // 1..32 bits
uint64_t extractBits64(uint16_t bitPos, uint8_t bitLen) const;  // 1..64 bits
void     setBits      (uint16_t bitPos, uint8_t bitLen, uint32_t value);  // 1..32 bits
template<typename E> E extractEnum(uint16_t bitPos, uint8_t bitLen, uint8_t maxValid, E dflt) const;
static void printHex(Stream& s, uint32_t value, uint8_t digits);  // zero-padded hex for debugPrint()
```

The bit position is 16 bits (ADR-012), and a field outside the buffer reads 0 or is not
written. These helpers are the library's only bit-extraction API: the legacy
`include/util/TPS25751BitUtils.h` free function `extractBits(const uint8_t*, uint8_t,
uint8_t)`, with an 8-bit position and no bounds check, was removed on 2026-11-19.

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
#define DEBUG_LEVEL_WARN     2
#define DEBUG_LEVEL_INFO     3
#define DEBUG_LEVEL_DEBUG    4
#define DEBUG_LEVEL_TRACE    5

class TPS25751Debug {
public:
    static void print(uint8_t category, uint8_t level, const char* message);
    static void setLevel(uint8_t level);
    static void enableCategory(uint8_t category);
    static void disableCategory(uint8_t category);
};
```

**Compile-time vs. runtime debug control.** The verbosity is governed by two
*independent* macros so that runtime control works out of the box while builds can
still strip debug code for size:

- `DEBUG_LEVEL` — the **runtime default**, the initial value of
  `TPS25751Debug::currentDebugLevel` before `setDebugLevel()` is called. Defaults
  to `DEBUG_LEVEL_NONE` (silent).
- `DEBUG_LEVEL_MAX` — the **compile-time ceiling**. The `TPS_DEBUG_*` macros are
  gated by `#if DEBUG_LEVEL_MAX >= <level>`; anything above the ceiling expands to
  nothing and is removed from the binary. Defaults to `DEBUG_LEVEL_TRACE` so all
  levels are compiled in and `setDebugLevel()` can enable any of them with no
  build flag.

Because the ceiling defaults to `TRACE`, `TPS25751::setDebugLevel(level)` is fully
effective at runtime without `-DDEBUG_LEVEL=...`. For **size-constrained / release
builds**, lower the ceiling (e.g. `-DDEBUG_LEVEL_MAX=DEBUG_LEVEL_NONE`) to strip
the higher-level debug code — `setDebugLevel()` then cannot raise verbosity above
the ceiling, since that code no longer exists.

> Historical note: `DEBUG_LEVEL` alone used to gate compilation (defaulting to 0),
> so without `-DDEBUG_LEVEL=5` every `TPS_DEBUG_*` call was stripped and
> `setDebugLevel()` had no effect. Splitting the ceiling into `DEBUG_LEVEL_MAX`
> fixed that.

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
TPS25751Register (abstract base — shared decoder base for ALL register classes)
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
└── ... (downstream device register classes extend this same base — e.g. the 57
         BQ25798 register classes in the sibling lib/BQ25798 repo)

TPS25751RegisterFactory (abstract factory)
└── TPS25751RegisterFactoryImpl (concrete factory)

TPS25751Factory (singleton)

TPS25751 (main controller)
└── executeCommand() — 4CC command-task executor (COMMAND/DATA handshake)

TPS25751DownstreamDevice (I2Cc proxy; composes a TPS25751 host, not a subclass)
└── <Device>::Device (typed per-device drivers inherit this — reference
                      implementation: BQ25798::Device, sibling lib/BQ25798 repo)

TPS25751Debug (utility, all static)
```

**Note:** `TPS25751Register` is a device-agnostic decoder base — downstream device
register classes (for parts on the TPS25751's I2Cc bus) extend it exactly like
host-side registers. Each device maintains its own Address enum, RegisterInfo table,
and factory (mirroring `TPS25751RegisterFactory` / `TPS25751Factory`); the host
factory is unaffected by downstream additions.

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
   - TPS25751Register::extractBits*(bitPos, bitLen)
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

### Downstream-Device Typed Write Flow (ADR-009)

The typed write tier defined by ADR-009 is a **read-modify-write of a value object**
layered on the I2Cw proxy: read the register live via I2Cr, apply a field setter that
mutates only its own bits, write the whole buffer back via an I2Cw task
(`len ≤ 11`, `Length = payload + 1`; see ADR-007). The step-by-step walkthrough of
this flow through the reference implementation (`BQ25798::Device`) moved with the
driver to the sibling **`lib/BQ25798`** repository — see
`docs/engineering/CONVENTIONS.md` there.

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

The BQ25798 driver (`BQ25798::Device`, maintained in the sibling **`lib/BQ25798`**
repository) is the reference implementation — follow its layout when adding a new
downstream device. A driver is a separate library that depends on this one; it
needs no changes to any TPS25751 host file.

**Recipe (concrete steps mirroring the BQ25798 implementation):**

1. **Create `include/<Device>/` and `src/<Device>/`** for all device-specific headers
   and sources. Use a `namespace <Device>` throughout.
2. **Define `<Device>::Registers::Address`** (enum of hardware register addresses)
   and a `RegisterInfo` table — mirrors `TPS25751Registers` and
   `TPS25751RegisterAddress.h`.
3. **Implement register classes** under `include/<Device>/` (one class per register),
   each inheriting `TPS25751Register`. Follow the same mandatory template as
   host-side registers: default constructor + `(const uint8_t*, size_t)` constructor,
   three-tier validation (`isSemanticallyValid()`, or `validateBasic/Data/Semantic`
   as appropriate), `debugPrint()` with `F()`. Keep read-only registers decode-only;
   for R/W registers add a `static constexpr Registers::Address kAddress` and field
   setters following the write-encoder pattern (ADR-009, STANDARDS.md).
   - **Multi-byte (16-bit) registers on big-endian downstream devices**: assemble
     `(raw[0] << 8) | raw[1]`, NOT `TPS25751Register::extractBits16` (little-endian;
     will silently byte-swap the value).
   - **Signed ADC channels**: reinterpret the assembled uint16_t via `int16_t` (2's
     complement); do not treat as unsigned.
   - **ADC LSB step sizes**: use values from the device datasheet directly; MCP
     register definitions may not include conversion factors.
4. **Create `<Device>::RegisterFactory` / `RegisterFactoryImpl` / `Factory`**
   (same abstract interface + singleton pattern as `TPS25751RegisterFactory` /
   `TPS25751Factory`, but scoped to the device). The factory switch dispatches on
   `<Device>::Registers::Address`; unknown addresses return `nullptr`.
5. **Create `<Device>::Device : public TPS25751DownstreamDevice`** in
   `include/<Device>/<Device>Device.h`. Add one typed `read<Register>()` method per
   register (calls the inherited `readRegister<T>()` template; returns
   `std::unique_ptr<T>`). For write-capable registers, add the typed write tier:
   a generic `writeRegister<T>()` (keyed on `T::kAddress`, sends `reg.raw()/reg.size()`
   via the inherited raw `writeRegister(addr, data, len)`), an `updateRegister<T>()`
   read-modify-write helper, and optional L3 convenience setters (see ADR-009).
6. **Respect the TRM's 5-second minimum spacing** between consecutive I2Cr (or
   consecutive I2Cw) commands — `TPS25751DownstreamDevice` warns via
   `DEBUG_CAT_TASK` if violated but does not block the call.
7. **Register the device's MCP docs server** in the driver repo's `.mcp.json`,
   pointing the generic server in the sibling `lib/device-register-mcp-server` repo
   (`uv --directory ../device-register-mcp-server run server.py`) at a new `devices/<device>/`
   manifest authored there, and update the MCP table in the driver repo's
   `AGENTS.md`.

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
TPS25751Register bit helpers (bit extraction)
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

### ADR-008: Downstream-Device Driver Tier (BQ25798)
**Status:** Accepted
**Date:** 2026-06
**Note (2026-07):** The reference implementation (`BQ25798::Device`) now lives in
the sibling **`lib/BQ25798`** repository as a separate library depending on this
one. This ADR remains here because it defines the *extension-point contract* any
downstream driver must follow; the split itself validated the contract (the driver
moved out with zero source edits).
**Context:** `TPS25751DownstreamDevice` provides raw I2Cr/I2Cw proxy access, but
callers need typed, validated register objects for the BQ25798 charger — without
coupling the host TPS25751 factory to a third-party device.
**Decision:**
- The typed driver **inherits** `TPS25751DownstreamDevice` (e.g. `BQ25798::Device`)
  rather than composing it, so the driver is itself the proxy with zero extra
  indirection.
- All driver code lives in a dedicated `namespace <Device>` with its own directories
  (`include/<Device>/`, `src/<Device>/`), fully decoupled from the host namespace.
- Each downstream device maintains its own `Address` enum, `RegisterInfo` table, and
  factory (`<Device>::RegisterFactory` / `RegisterFactoryImpl` / `Factory` singleton),
  **mirroring the host-side pattern exactly**. The host factory is not modified.
- Downstream device register classes reuse `TPS25751Register` as their decoder base —
  they look and behave identically to host-side registers from the caller's
  perspective. Read-only registers stay decode-only; R/W registers also carry field
  setters and a `kAddress` for the typed write tier (see ADR-009).
- Writes go through the inherited `TPS25751DownstreamDevice::writeRegister()`, or
  driver-specific setter methods that validate and encode before calling it.
**Consequences:**
- Adding a new downstream device is self-contained: new directories + namespace +
  factory, no changes to any TPS25751 host files.
- **Big-endian 16-bit registers**: downstream devices (e.g. BQ25798) store 16-bit
  fields big-endian; assemble as `(raw[0] << 8) | raw[1]`, not via
  `extractBits16` (which is little-endian and silently byte-swaps the value).
- **Signed ADC channels** (IBUS, IBAT, TDIE on BQ25798): assembled uint16_t must be
  reinterpreted as `int16_t` (2's complement); treating as unsigned gives wrong sign.
- **ADC LSB step sizes** must be sourced from the device datasheet (SLUSE02 for
  BQ25798); the MCP register definitions do not carry conversion factors.
- The BQ25798 implementation (57 registers, sibling `lib/BQ25798` repo) is the
  canonical reference for future downstream device drivers.

### ADR-009: Downstream-Device Write Encoders
**Status:** Accepted
**Date:** 2026-06
**Context:** ADR-008 established the decode (read) tier for downstream devices and
left field-level encoders deferred. Callers now need to *write* the ~31 R/W BQ25798
registers (charge/voltage/current limits, enables, watchdog, …) with the same typed,
field-level ergonomics as the read tier — without re-reading-and-clobbering reserved
or sibling bits.
**Decision:**
- **Read-modify-write value-object model.** A register object owns its raw bytes
  (`TPS25751Register::raw()` / `size()`, exposed on the base for this purpose). A field
  setter mutates only its field's bits in that buffer; everything else (reserved bits,
  sibling fields) is preserved by construction. The whole buffer is then written back.
- **Encode helpers** (`include/BQ25798/BQ25798Encode.h`, `lib/BQ25798` repo): `setField8()` and
  `setField16BE()` are the inverse of the decode-side extraction. They read-modify-write
  only the field's bits and mask the value to the field width. `setField16BE()` is
  **big-endian** (`raw[0]=MSB`), matching the read side's `(raw[0]<<8)|raw[1]`.
- **Generic typed write** — `Device::writeRegister<T>(const T&)` keys on the register's
  `static constexpr T::kAddress` (the single source of register identity) and sends
  `reg.raw()/reg.size()` through the inherited raw I2Cw path. A
  `using TPS25751DownstreamDevice::writeRegister;` keeps the inherited raw overload
  visible alongside the template.
- **`Device::updateRegister<T>(Fn&& mutate)`** — read `T` live, apply the mutator, write
  it back; one-call read-modify-write. The 10 L3 convenience setters
  (`enableCharging`, `setChargeCurrentLimit`, …) are thin `updateRegister<T>` wrappers.
- **Field setters invert the class's *own* decode constants.** An engineering-unit
  setter (e.g. `setMillivolts`) reuses the same `kLsbMv`/`kOffsetMv` the decode accessor
  uses, so encode and decode can never disagree. Offset conversions guard against
  unsigned underflow (clamp to 0 below the offset). Every setter is guarded
  `if (!isValid()) return;`.
**Consequences:**
- Reserved bits are preserved automatically — there is no separate "reassemble the whole
  register" step that could drop or zero them.
- Cross-references ADR-008 (decode tier): the same big-endian and own-constant rules now
  apply symmetrically on encode. The I2Cw framing constraints (`len ≤ 11`,
  `Length = payload + 1`) from ADR-007 are unchanged and apply to typed writes.
- Read-only registers (Status, Flags, ADC results, IcoCurrentLimit, PartInfo) get no
  `kAddress` and no setters, so they cannot be written by mistake.
- Canonical examples (sibling `lib/BQ25798` repo): `BQ25798ChargerControl0`
  (single-bit), `BQ25798ChargeVoltageLimit` (16-bit BE + unit inversion),
  `BQ25798MinimalSystemVoltage` (offset inversion with underflow guard).

### ADR-010: Dual-Transport Downstream Devices — Optional Direct I2C
**Status:** Accepted
**Date:** 2026-06
**Context:** `TPS25751DownstreamDevice` (and thus `BQ25798::Device`) could only reach a
part *through* the TPS25751, encoding every access as an I2Cr/I2Cw 4CC task (ADR-007).
That is correct for the production topology (charger on the TPS25751's I2Cc bus) but
unusable for bench bring-up, validation, or boards where the part is wired straight to
the MCU's own I2C bus. The TRM 5 s spacing and the 63-byte read / 11-byte write payload
caps are artifacts of the proxy path, not of the part.
**Decision:**
- **Transport is selected at construction, branched at one chokepoint.** A second
  constructor `TPS25751DownstreamDevice(TwoWire&, addr)` selects a **direct** transport;
  the existing `(const TPS25751&, addr)` constructor keeps the **proxied** transport. The
  host member becomes a nullable `const TPS25751*` and a `TwoWire*` is added; a `_direct`
  flag picks the path. The two raw methods `readRegister()` / `writeRegister()` branch to
  `readDirect`/`writeDirect` (plain Arduino-`Wire` register transactions) or
  `readProxied`/`writeProxied` (the unchanged I2Cr/I2Cw bodies).
- **Everything above the chokepoint is untouched.** All 50+ `BQ25798::Device` typed
  accessors, the generic `writeRegister<T>`/`updateRegister<T>`, and the L3 setters funnel
  through those two raw methods, so they work identically in either transport with no code
  change. `BQ25798::Device` just gains a forwarding `Device(TwoWire&, addr)` constructor.
- **Direct mode is standard I2C.** Read = write register pointer, repeated-start,
  burst-read N bytes (the part auto-increments its pointer); write = register pointer +
  payload + STOP. This is the part's own protocol, *not* the TPS25751's length-byte
  register protocol and *not* the 4CC task path.

**Consequences:**
- Direct mode has **no** TRM 5 s spacing and **no** 63/11-byte caps — the only bound is
  the `TwoWire` buffer. It requires the part to actually be on the passed bus.
- Any future downstream-device driver built on `TPS25751DownstreamDevice` inherits direct
  mode for free.
- The big-endian decode/encode conventions (ADR-008/009) are unchanged: burst reads/writes
  carry multi-byte registers identically over either transport.

### ADR-011: Patch Loader — Non-Register PBM Sequencer on `executeCommand()`
**Status:** Accepted
**Date:** 2026-06
**Context:** The TPS25751 can boot in PTCH (patch) mode when no EEPROM is present and
receive its firmware+configuration image directly over I2Ct via Patch Burst Mode (PBM),
documented in TI app note SLVAFV8A.  The PBM flow uses two 4CC command-tasks (`PBMs`
to start the session, `PBMc` to commit it) plus a raw burst write to I2C address `0x30`
(no register number, no length byte — unlike every other TPS25751 write).
**Decision:**
- **`TPS25751PatchLoader` is not a register.** The PBM flow has no fixed register layout
  and does not benefit from the factory / three-tier validation machinery. The loader
  deliberately does not inherit `TPS25751Register` and is not wired into
  `TPS25751RegisterFactory`. Reviewers should not expect the register-class template.
- **Reuse `executeCommand()` for both 4CC commands.** `PBMs` and `PBMc` are submitted
  via `TPS25751::executeCommand()` like any other task; DATA staging, COMMAND write,
  COMMAND polling, `"!CMD"` detection, and DATA read-back are all handled by the executor.
  The loader supplies only the payload bytes and maps `TPS25751TaskResult` to
  `PatchLoadStatus`.
- **One new core primitive: `TPS25751::burstWrite()`.** The raw burst to `0x30` (no
  register number, no length byte) is the only I2C access shape `executeCommand()` cannot
  cover. It is implemented as a public method on `TPS25751`, loops
  `beginTransmission/write/endTransmission(true)` per chunk of `min(chunk, remaining)`
  bytes, and reuses `TPS_DEBUG_*` / `TPS_REPORT_I2C_ERROR` exactly like `writeRegister`.
  The default chunk size is `PBM_BURST_CHUNK` (32 bytes, matching the stock Wire TX
  buffer); the PBM pointer auto-increments across I2C START/STOP so chunking is safe
  (SLVAFV8A footnote 3).
- **`PatchLoadStatus` provides one distinct value per failure point** in the 17-step
  sequence, giving callers a precise diagnostic rather than a boolean.
- **IRQ-gated inter-command waiting is optional** (`irqPin = -1` defaults to pure
  polling). Command-completion polling is always done by `executeCommand()`'s internal
  `waitForCommandClear()`; the IRQ pin is used only for the "Ready-for-Patch" step 1
  signal, which the polling mode skips.
**Consequences:**
- `TPS25751PatchLoader` is a standalone sequencer (`include/TPS25751PatchLoader.h`,
  `src/TPS25751PatchLoader.cpp`) that composes a `TPS25751&` reference. It forward-
  declares `TPS25751` in its header to avoid a circular include (TPS25751.h includes
  TPS25751PatchLoader.h at its end for transparent consumer access).
- The `0x30` argument to `burstWrite()` is a 7-bit I2C *device address* (the PBM receive
  port), not a TPS25751 register offset — these happen to share the numeric value with
  `TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES` but are entirely unrelated.
- BQ25798 `bundleSize` in the `PBMs` config is **little-endian** (4 bytes, LSB first),
  consistent with host-register byte order but opposite to BQ25798 downstream registers
  (which are big-endian). See CONSTRAINTS.md "Patch Burst Mode (PBM)".


### ADR-012: 16-Bit Bit Positions in the Register Base Class
**Status:** Accepted
**Date:** 2026-11-19
**Context:** `extractBits()`, `extractBits16/32/64()`, `setBits()`, and the `extractEnum*()`
templates took the field's bit position as `uint8_t`, so no field could start past bit 255
(byte 31). Seven registers are longer than 32 bytes. DATA (0x09) and the four capability
registers (0x30 to 0x33) reach their late bytes through byte accessors or hand-assembled PDO
words, and two registers with individually defined fields past bit 255 worked around the
limit on their own: IO_CONFIG (0x5C, 49 bytes, fields to bit 391) and
TX_BATTERY_CAPABILITIES (0x7D, 36 bytes, battery 3 at bits 287:216).
`TPS25751IOConfig` assembled every field from `_raw[]` by hand, and
`TPS25751TransmitBatteryCapabilities` carried four class-private, bounds-checked byte
helpers for reads and writes. Both copies repeated bounds logic the base class already
has, and every new long register would have added another.
**Decision:**
- Widen `bitPos` to `uint16_t` in every instance and static extract and set method and in
  the `extractEnum*()` templates. `bitLen` stays `uint8_t`, because no field is wider than
  64 bits.
- Do all internal byte arithmetic (start byte, bytes needed, loop indexes) in `size_t`, and
  compare the field's end against the buffer length in `size_t`, so no sum can wrap.
  `extractBits64()` computes the upper half's bit position in 32 bits and reads that half
  as 0 when the position does not fit, instead of letting it wrap to a low bit. Each half
  stays bounds-checked on its own, as before.
- Convert 0x7D and 0x5C to the base-class calls and delete their private helpers.
- `uint16_t` rather than `size_t`: 65,535 bits is 8 KB, far past the 64-byte DATA register,
  and it matches the existing `uint16_t startBitPos` of `extractArray32()`.
**Consequences:**
- Any field inside the buffer is reachable; the only limit is the register's length.
- The change is source-compatible. Callers pass integer literals or `uint8_t` values, which
  convert implicitly, including the sibling BQ25798 driver, which calls the protected
  helpers. A derived class that declares its own `extractBits(uint8_t, uint8_t)` overload
  would now overload rather than hide the base method; no class in this repository or in
  BQ25798 does.
- Decode results do not change. Native tests cover fields at bit 255, past it, and
  straddling it, and a byte-for-byte comparison of `debugPrint()` for all 37 registers
  matched before and after.
- The PDO accessors of the capability registers (0x30 to 0x33) still assemble their 32-bit
  words from `_raw[]` by hand. They are correct and were left as they are; they could move
  to `extractBits32()` and `setBits()` in a later cleanup.


### ADR-013: USB PD Objects as Value Types
**Status:** Accepted
**Date:** 2026-11-22
**Context:** Registers 0x30 to 0x35 carry USB PD PDO and RDO words. Each class decoded
them with its own getters (`getFixedVoltage_mV(i)`, `fixedUnconstrainedPower()`,
`operatingCurrent_mA()`, and so on), so five copies of the PDO decode drifted apart and
shared the same faults: fixed-PDO flags read from any position although Table 6.10
reserves them outside PDO 1, no decode of the SPR AVS APDO the board itself
advertises, every RDO read as Fixed/Variable whatever the contract, and per-class
validation rules that disagreed with the specification.
**Decision:**
- One value type per object: `TPS25751Pdo` (a PDO word, its 1-based position, and the
  role of its sender) and `TPS25751Rdo` (an RDO word and its `RdoKind`). Both decode
  from USB PD R3.3 V1.0 tables served by `usb-pd-docs`, return 0 or false for a field
  that does not apply, and print themselves.
- The register classes hand them out: `pdo(i)` in 0x30 to 0x33, `activePdo()` and
  `firstPdoControl()` in 0x34, `ownRdo(kind)` and `requestedRdo(kind)` in 0x35. The
  per-class field getters are removed.
- An RDO's kind is not in the word. The caller derives it with `rdoKindFor(pdo)` from
  the PDO at the RDO's Object Position, and 0x35 offers a `debugPrint()` overload that
  takes both kinds.
- `validatePdoList()` validates a whole Capabilities list once, with the received or
  transmit rule set, so a list rule (PDO 1 is vSafe5V, the PDO order, AVS currents
  matching the Fixed PDOs) has one implementation.
**Consequences:**
- A breaking API change in 3.0.0: the per-class PDO and RDO getters are gone, and
  0x34's raw accessors are renamed `rawActivePdo()` and `rawFirstPdoControlBits()`.
- Decoding a new PDO or RDO layout, or changing a rule, is a change to one file and
  one suite (`test_TPS25751Pdo`, `test_TPS25751Rdo`).
- 0x30 to 0x34 validation follows USB PD R3.3 (received lists reject only what a
  receiver must; transmit lists also enforce the sender rules), which accepts some
  values the old rules rejected (zero currents) and rejects some they accepted (a PDO
  1 at 4.9 V, an over-range current).
- 0x35 cannot decode its typed fields alone; its single-argument `debugPrint()` prints
  the raw words and common fields with a note that the typed fields need the PDO type.

### ADR-014: EEPROM Updater, a Non-Register Flash-Task Sequencer on an Executor Seam
**Status:** Accepted
**Date:** 2026-11-23
**Context:** The TPS25751 boots its patch bundle from an external EEPROM at 0x50 on its
I2Cc bus. A host on I2Ct rewrites that EEPROM only through four flash 4CC tasks, `FLrd`,
`FLad`, `FLwd`, and `FLvy` (TRM SLVUCR8B Tables 4-19 to 4-22, pages 81 and 82), while the
controller runs in APP. TI application note SLVAFL1 defines a two-region layout and a
four-step update in which an erased region pointer keeps the controller from loading a
region while it is written. An ordering error can leave no bootable region; recovery then
needs a PBM load (ADR-011) or an external programmer. The sequencing therefore needs
host-run tests that inject a fault at every task, and `src/TPS25751.cpp`, which owns
`executeCommand()`, cannot run on the host because it needs `Wire`.
**Decision:**
- `TPS25751EepromUpdater` is a sequencer, not a register, beside `TPS25751PatchLoader`: it
  does not inherit `TPS25751Register` and is not in the factory. It provides the task
  primitives, checked region pointer writes, `updateRegion()`, `updateInactiveRegion()`,
  `updateBothRegions()`, `programBlank()`, `compareRegion()`, `dump()`, and `coldReset()`.
- It depends on `TPS25751CommandExecutor`, an interface of five calls (`executeCommand()`,
  `readMode()`, `readBootFlags()`, `delayMs()`, `nowMs()`) that does not include `Wire.h`.
  `TPS25751HostExecutor` adapts a `TPS25751` to it on the target, and the native tests use
  `FakeCommandExecutor`. Virtual calls need no RTTI, so the `-fno-rtti` constraint holds.
  `TPS25751` itself is unchanged.
- Region selection reads the pointers first and BOOT_FLAGS (0x2D, BOOT_STATUS in SLVAFL1)
  second, and only when both pointers are valid. BOOT_FLAGS is latched at boot. After an
  update without a restart it describes the old layout, and SLVAFL1 Figure 2-2, which
  decides from BOOT_FLAGS alone, would then select the region whose pointer is the only
  valid one and erase it. Hardware stage 3 of the plan reached that state and the
  pointer-first rule chose correctly.
- The written region is compared with the image through `FLrd` before its pointer is
  written. The TRM says `FLvy` completes after the header is checked. Stage 3 saw it reject
  a bundle with one inverted body byte, which is a sample of one, and SLVAFL1 (page 2)
  says a CRC error in a Low region with a correct header ends the boot without trying
  High, so a valid Low pointer over a damaged Low body costs both regions. A High CRC
  error does fall back to Low.
- Step 2 of an update reads back before it trusts the controller's write address. Before
  any task it checks that the 12 bytes after the erased pointer are not the start of the
  image, the trace of an earlier misplaced write. After the first two chunks it reads the
  target pointer row and then the chunks, compares both, and sends a fresh `FLad` before
  the rest. An `FLad` that reported success without taking effect, a case no TI document
  describes, then misplaces 64 bytes into the unused bytes after the pointer instead of a
  whole bundle across the other region, in the case the tests model: a write address left
  just past the pointer word.
- `coldReset()` (`GAID`) is a separate call, so the application decides when the port
  disconnects. It returns `Success` only when MODE reads APP after the call saw the
  controller go away (an `I2CError` on `GAID` or a failed MODE read), and
  `ResetNotObserved` for APP without that evidence, because MODE reads APP before and
  after a restart (decided 2026-11-23).
- `programBlank()` writes both AppConfigOffset words to 0 first, then runs steps 1 to 3 of
  `updateRegion()` per region, Low first, reading the region's word once more before its
  final pointer write, and refuses while any pointer is valid (decided 2026-11-23). The last
  write before each bundle `FLad` is then the pointer erase, as in `updateRegion()`. It is
  the flash-task half of the recovery in SLVAFL1 Figure 4-1, after a PBM load.
**Consequences:**
- The updater builds in `[env:native]` and `test/coverage.sh` measures it. Its suite
  checks every call sequence against the plan and sweeps an executor failure and a wrong
  return code across every task of an update, asserting the EEPROM state the status table
  promises.
- `FakeCommandExecutor` models behaviour the TRM does not state, such as an `FLwd` at a
  stale write address and an `FLvy` that checks only Header_ID. A test that depends on such
  a choice pins the fake, not the device; the fake labels each one.
- The read-back of step 2 has residual limits. The `FLad` after the read-back is not
  verified, a write address that goes wrong later shows only in step 3 and only inside the
  target region, and when the target already held the image, chunks misplaced outside the
  pointer row go unseen. `programBlank()` inherits these limits per pass; a sweep of an
  `FLad` without effect at each of its 10 `FLad` calls, with and without an `FLrd` that
  moves the write address, finds no valid pointer over a region that does not hold the
  image. Whether an `FLrd` moves the write address is not documented and has not been
  observed.
- Hardware stages 1 to 4 verified the layout, the result offsets, a 4-byte `FLwd`, a full
  region update, `updateBothRegions()`, `GAID`, and the boot region after a cut at each
  step. `programBlank()`, the I2C EEPROM Present flag after a PBM boot, and the
  `ResetNotObserved` rule against a second firmware wait for stage 5 or later.
- A region update blocks for about 26 s and both regions for about 52 s (stage 3). An
  application with a watchdog services it from the progress callback.
- `TPS25751PatchLoader` and `TPS25751DownstreamDevice` still call `TPS25751` directly.
  Moving them to the executor seam would give them native tests too; it is not planned.

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial architecture document |
| 1.1 | 2026-06-22 | Claude Code | Added 4CC command-task layer, I2Cc downstream-device proxy (ADR-007), register write flow |
| 1.2 | 2026-06-24 | Claude Code | Added BQ25798 downstream device driver tier (ADR-008), updated inheritance tree, extension-point recipe |
| 1.3 | 2026-06-24 | Claude Code | Added downstream-device write encoders (ADR-009), typed write flow; updated ADR-008/driver-layer to note R/W setters + kAddress |
| 1.4 | 2026-06-26 | Claude Code | Added dual-transport downstream devices (ADR-010): optional direct-I2C constructor on TPS25751DownstreamDevice / BQ25798::Device |
| 1.5 | 2026-07-02 | Claude Code | BQ25798 driver split out to the sibling lib/BQ25798 repo; ADR-008/009/010 retained as the downstream-driver extension-point contract, BQ25798-specific walkthrough moved with the driver |
| 1.6 | 2026-11-19 | Claude Code | Added ADR-012: 16-bit bit positions in the register base class; moved ADR-010's Consequences back under ADR-010 |
| 1.7 | 2026-11-22 | Claude Code | Added ADR-013: USB PD PDO and RDO words decode through the TPS25751Pdo and TPS25751Rdo value types |
| 1.8 | 2026-11-23 | Claude Code | Added ADR-014: TPS25751EepromUpdater, a non-register flash-task sequencer on the TPS25751CommandExecutor seam |

---

*Understanding this architecture ensures consistent, maintainable implementations.*
