# AGENTS.md

This file provides guidance to AI coding assistants when working with code in this repository.

---

## Project Overview

This is a PlatformIO-based Arduino project for the Teensy platform that demonstrates usage of the TPS25751 USB-C Power Delivery controller. It's an example project that continuously reads and displays various register values from the TPS25751 chip via I2C communication.

**Last Updated:** 2025-10-20
**Project Type:** Example/Demo Application

### Key Components

- **Example Application** (`src/main.cpp`): Demo loop reading TPS25751 registers every second
- **TPS25751 Library** (`../../`): Custom library for interfacing with the TPS25751 chip
- **Hardware Target**: Teensy 4.0 and 4.1 boards
- **Communication**: I2C at addresses 0x20-0x23

### For Library Development

**IMPORTANT**: If you're working on the TPS25751 library itself (register classes, factory pattern, validation, etc.), see **[../../AGENTS.md](../../AGENTS.md)** for:
- Engineering standards and constraints
- Register implementation guidelines
- Architecture and design patterns
- Testing requirements
- Development workflow

This file focuses on the **example project** that uses the library.

---

## Build and Development Commands

### Building
```bash
# Build for default environment (teensy41)
pio run

# Build for specific environment
pio run -e teensy40
pio run -e teensy41

# Clean build
pio run -t clean
```

### Uploading and Monitoring
```bash
# Upload firmware to connected device
pio run -t upload

# Monitor serial output (115200 baud)
pio device monitor

# Upload and monitor in one command
pio run -t upload && pio device monitor
```

### Testing
```bash
# Run unit tests
pio test

# Run tests with coverage
pio test --coverage
```

---

## Example Application Architecture

### Main Application (`src/main.cpp`)

The example demonstrates basic TPS25751 usage:

```cpp
setup() {
  // Initialize serial communication (115200 baud)
  // Initialize I2C communication
  // Create TPS25751 controller instance
}

loop() {
  // Read various registers:
  //   - STATUS: Connection state, VBUS status, power role
  //   - MODE: Operating mode and capabilities
  //   - POWER_PATH_STATUS: Power path and charging
  //   - BOOT_FLAGS: Boot and initialization status
  //   - PORT_CONFIG: Port configuration
  // Print register values via Serial
  // Wait 1 second
}
```

### Hardware Configuration

- **Target Platforms**: Teensy 4.0 and 4.1 (ARM Cortex-M7)
- **I2C Addresses**: Supports 4 default addresses (0x20-0x23)
  - Configured via `TPS25751_I2CT_ADDRESS_IDX` in platformio.ini
- **Debug Level**: Set to level 2 via build flags
- **Serial Speed**: 115200 baud for monitoring
- **I2C Pins**: Default Teensy I2C pins (SDA/SCL)

### Using the Library

```cpp
#include <TPS25751.h>

// Create controller instance
TPS25751 tps;

// Read registers
auto status = tps.readStatusRegister(true);  // with validation
status.debugPrint();

// Access register fields
if (status.isPluggedIn()) {
  Serial.println("Device connected");
}
```

For complete library documentation, see [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md).

---

## Project Documentation Structure

```
/AGENTS.md                              # This file - Example project guide
/src/
  └── main.cpp                          # Example application
/lib/TPS25751/
  ├── AGENTS.md                         # Library developer guide (START HERE for library work)
  ├── include/                          # Public header files
  ├── src/                              # Library implementation
  └── docs/
      ├── AGENTS.md                     # MCP server documentation
      ├── engineering/                  # Engineering standards (REQUIRED READING)
      │   ├── STANDARDS.md              # Implementation standards
      │   ├── CONSTRAINTS.md            # Platform constraints
      │   ├── ARCHITECTURE.md           # System design
      │   ├── TESTING.md                # Testing requirements
      │   └── DOCUMENTATION.md          # Documentation standards
      ├── plans/
      │   └── plan-impl-v01.md          # Implementation plan (12 registers remaining)
      └── mcp-servers/
          └── tps25751-docs/            # MCP server for register documentation
              └── AGENTS.md             # MCP server usage guide
/platformio.ini                         # PlatformIO configuration
```

### Navigation Guide

**For Example Project Work:**
- Read this file (AGENTS.md)
- Review src/main.cpp
- Check platformio.ini for build configuration

**For Library Development:**
- **START HERE**: [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md)
- Engineering Standards: [lib/TPS25751/docs/engineering/STANDARDS.md](lib/TPS25751/docs/engineering/STANDARDS.md)
- Platform Constraints: [lib/TPS25751/docs/engineering/CONSTRAINTS.md](lib/TPS25751/docs/engineering/CONSTRAINTS.md)
- Architecture: [lib/TPS25751/docs/engineering/ARCHITECTURE.md](lib/TPS25751/docs/engineering/ARCHITECTURE.md)

**For Code Analysis:**
- Use IDE index tools (e.g., `ide_find_class`, `ide_find_references` via the `clion-index` MCP server if available) for code navigation and analysis
- Search classes, functions, understand inheritance, trace call graphs

**For Register Documentation:**
- MCP Server Guide: [lib/TPS25751/docs/mcp-servers/device-register-docs/AGENTS.md](lib/TPS25751/docs/mcp-servers/device-register-docs/AGENTS.md)
- Use `search_register`, `explain_bitfield`, `get_register` MCP tools (`tps25751-docs` for the PD controller, `bq25798-docs` for the charger)

---

## Known Issues

### Example Project Issues

1. **Main loop bug** (src/main.cpp:47-50)
   - `bootFlags.debugPrint()` called instead of `portConfig.debugPrint()`
   - Low priority, doesn't affect functionality

### Library Issues

For library-specific issues, see [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md#known-issues).

---

## Development Notes

### Project-Specific

- The project includes a local copy of the TPS25751 library in `lib/TPS25751/`
- There's a commented symlink reference in `platformio.ini` suggesting this library may eventually be published
- Serial monitor output shows all register values every second

### Library-Specific

For library development notes, architecture decisions, and implementation details, see [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md).

---

## Quick Start for AI Assistants

### Working on the Example Application

1. **Read this file** for project context
2. **Check src/main.cpp** to understand current functionality
4. **Modify and test** using PlatformIO commands above
5. **Monitor output** at 115200 baud

### Working on the Library

1. **READ [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md)** - Library developer guide
2. **READ [lib/TPS25751/docs/engineering/STANDARDS.md](lib/TPS25751/docs/engineering/STANDARDS.md)** - Required reading
3. **READ [lib/TPS25751/docs/engineering/CONSTRAINTS.md](lib/TPS25751/docs/engineering/CONSTRAINTS.md)** - Platform gotchas
4. **Use MCP tools** for register information:
   - `tps25751-docs` - Register details (`search_register`, `explain_bitfield`)
5. **Follow the checklist** in the library AGENTS.md

### Common Tasks

**Add a new register to the example:**
1. Check if register class exists in `lib/TPS25751/include/` (look for `TPS25751RegisterName.h`)
2. If not, implement following library standards (see lib/TPS25751/AGENTS.md)
3. Add read call in main.cpp loop
4. Add debugPrint() call to display results

**Modify hardware configuration:**
1. Update platformio.ini build flags
2. Update src/main.cpp setup() if needed
3. Rebuild and upload

**Debug I2C communication:**
1. Set debug level in platformio.ini
2. Check Serial monitor output
3. See library AGENTS.md for I2C protocol details

---

## Getting Help

### Documentation

- **Example Project**: This file (AGENTS.md)
- **Library Development**: [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md)
- **Register Details**: Use `tps25751-docs` MCP server
- **TI Documentation**: lib/TPS25751/docs/ (TRM and datasheets)

### Troubleshooting

- **Build errors**: Check platformio.ini configuration
- **Upload errors**: Verify Teensy board connected and recognized
- **Library errors**: See [lib/TPS25751/AGENTS.md](lib/TPS25751/AGENTS.md)
- **I2C errors**: Check wiring and address configuration

---
