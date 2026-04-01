# TPS25751 Arduino Library

An object-oriented C++ library for interfacing with the **Texas Instruments TPS25751** USB-C Power Delivery controller via I2C. Designed for **Teensy 4.x** (ARM Cortex-M7) and compatible with the Arduino framework.

## Features

- **15 register classes** implemented (of 26 total), covering all critical USB-C PD diagnostics
- **Type-safe register access** — each register has a dedicated class with named accessors
- **Three-tier validation** — basic, data, and semantic validation on every read
- **Factory pattern** — RTTI-free object creation compatible with `-fno-rtti` builds
- **Comprehensive debug output** — configurable by level and category
- **RAII memory management** — `std::unique_ptr` and move semantics throughout
- **USB PD contract parsing** — all four PDO types (Fixed, Variable, Battery, PPS) with correct unit conversions

## Hardware Requirements

| Component     | Details                                        |
|---------------|------------------------------------------------|
| MCU           | Teensy 4.0 or 4.1 (ARM Cortex-M7)              |
| IC            | Texas Instruments TPS25751 USB-C PD controller |
| Interface     | I2C (Wire library)                             |
| I2C address   | 0x20 – 0x23 (set by ADDR pins)                 |
| Interrupt pin | Active-low, connect to any digital input       |

The library uses the **Arduino Wire library** for I2C communication and follows the TPS25751's custom protocol (responses are prefixed with a length byte).

## Installation

### Arduino IDE

1. Download the repository as a `.zip` file
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library...**
3. Select the downloaded zip
4. The library will appear under **File → Examples → TPS25751**

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/forrestrae/TPS25751.git
```

Or reference a specific version tag:

```ini
lib_deps =
    https://github.com/forrestrae/TPS25751.git#v1.0.0
```

## Quick Start

```cpp
#include <TPS25751.h>
#include <TPS25751Status.h>
#include <TPS25751PDStatus.h>

const TPS25751 pd;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) delay(10);

    pd.begin();
}

void loop() {
    // Read STATUS register with validation
    if (auto status = pd.readStatusRegister(true)) {
        status->debugPrint();  // Human-readable output over Serial
    }

    // Read PD contract status
    if (auto pdStatus = pd.readPDStatusRegister(true)) {
        pdStatus->debugPrint();
    }

    delay(2000);
}
```

## Supported Registers

| Register                    | Address | Description                         | Status      |
|-----------------------------|---------|-------------------------------------|-------------|
| STATUS                      | 0x1A    | Connection state, VBUS, power role  | Implemented |
| MODE                        | 0x03    | Operating mode (Application/Boot)   | Implemented |
| POWER_PATH_STATUS           | 0x26    | Power path and charging state       | Implemented |
| BOOT_FLAGS                  | 0x2D    | Boot and initialization flags       | Implemented |
| PORT_CONFIGURATION          | 0x28    | Port configuration settings         | Implemented |
| TYPEC_STATE                 | 0x69    | Type-C state machine                | Implemented |
| INT_EVENT1                  | 0x14    | Interrupt event flags               | Implemented |
| POWER_STATUS                | 0x3F    | Power delivery status               | Implemented |
| PD_STATUS                   | 0x40    | PD contract status                  | Implemented |
| GPIO_STATUS                 | 0x72    | GPIO pin states                     | Implemented |
| PORT_CONTROL                | 0x29    | Port control settings               | Implemented |
| ACTIVE_PDO_CONTRACT         | 0x34    | Active PDO from negotiated contract | Implemented |
| ACTIVE_RDO_CONTRACT         | 0x35    | Active RDO from negotiated contract | Implemented |
| RECEIVED_SOURCE_CAPS        | 0x30    | Source capabilities from partner    | Implemented |
| RECEIVED_SINK_CAPS          | 0x31    | Sink capabilities from partner      | Implemented |
| *(11 additional registers)* |         | Data registers, PD commands, config | Planned     |

## API Overview

### TPS25751 — Main Controller

```cpp
TPS25751 pd;

pd.begin();                                      // Initialize I2C
pd.readStatusRegister(bool validate);            // → unique_ptr<TPS25751Status>
pd.readModeRegister(bool validate);              // → unique_ptr<TPS25751Mode>
pd.readPDStatusRegister(bool validate);          // → unique_ptr<TPS25751PDStatus>
pd.readPowerStatusRegister(bool validate);       // → unique_ptr<TPS25751PowerStatus>
pd.readPowerPathStatusRegister(bool validate);   // → unique_ptr<TPS25751PowerPathStatus>
pd.readBootFlagsRegister(bool validate);         // → unique_ptr<TPS25751BootFlags>
pd.readPortConfigRegister(bool validate);        // → unique_ptr<TPS25751PortConfig>
pd.readPortControlRegister(bool validate);       // → unique_ptr<TPS25751PortControl>
pd.readTypeCStateRegister(bool validate);        // → unique_ptr<TPS25751TypeCState>
pd.readInterruptEventRegister(bool validate);    // → unique_ptr<TPS25751InterruptEvent>
pd.readActivePDOContractRegister(bool validate); // → unique_ptr<TPS25751ActivePDOContract>
pd.readActiveRDOContractRegister(bool validate); // → unique_ptr<TPS25751ActiveRDOContract>
pd.readReceivedSourceCapsRegister(bool validate);// → unique_ptr<TPS25751ReceivedSourceCaps>
pd.readReceivedSinkCapsRegister(bool validate);  // → unique_ptr<TPS25751ReceivedSinkCaps>
pd.readGPIOStatusRegister(bool validate);        // → unique_ptr<TPS25751GPIOStatus>
```

All read methods return a `std::unique_ptr` to the register object, or `nullptr` on failure. Pass `true` to enable validation.

### Debug Configuration

```cpp
// Set verbosity level
TPS25751::setDebugLevel(DEBUG_LEVEL_OFF);    // No output
TPS25751::setDebugLevel(DEBUG_LEVEL_ERROR);  // Errors only
TPS25751::setDebugLevel(DEBUG_LEVEL_WARN);   // Warnings and errors
TPS25751::setDebugLevel(DEBUG_LEVEL_INFO);   // Informational
TPS25751::setDebugLevel(DEBUG_LEVEL_DEBUG);  // Verbose

// Filter by category (bitfield, combine with |)
TPS25751::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_REGISTER | DEBUG_CAT_VALIDATION);
```

### Interrupt Handling

```cpp
const int IRQ_PIN = 17;

void onInterrupt() {
    if (auto event = pd.readInterruptEventRegister(true))
        event->debugPrint();
}

void setup() {
    pinMode(IRQ_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(IRQ_PIN), onInterrupt, FALLING);
    pd.begin();
}
```

## Documentation

Official Texas Instruments documents for the TPS25751:

- [TPS25751 Datasheet](https://www.ti.com/lit/gpn/tps25751) — USB Type-C and USB PD Controller
- [TPS25751 Technical Reference Manual](https://www.ti.com/lit/pdf/slvucr8) — Register map and detailed functional description

## Platform Notes

- Compiled with `-fno-rtti` — `dynamic_cast` and `typeid` are unavailable; the library uses `static_cast` and the factory pattern throughout
- Requires C++17 or later
- Tested on Teensy 4.0 and 4.1; may work on other Arduino-compatible ARM platforms
- Flash footprint: ~99 KB (full example with all registers)

## Contributing

Contributions are welcome for non-commercial purposes. See [
`docs/engineering/STANDARDS.md`](docs/engineering/STANDARDS.md) for implementation requirements before submitting changes. All new register classes must follow the established architecture and include unit tests with >90% coverage.

## License

Copyright (c) 2025 Forrest Rae

This library is licensed under the **Polyform Noncommercial License 1.0.0**. You may use, copy, modify, and redistribute this software for non-commercial purposes. Commercial use requires a separate written agreement with the author.

See the [`LICENSE`](LICENSE) file for full terms.
