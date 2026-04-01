# TPS25751 Library - Developer Guide

This file provides guidance for working with the TPS25751 USB-C Power Delivery Controller Arduino library.

---

## Library Overview

The TPS25751 library is an object-oriented C++ library for interfacing with the Texas Instruments TPS25751 USB-C Power Delivery controller via I2C communication. It provides:

- Type-safe register access with dedicated classes for each register type
- Three-tier validation system (basic, data, semantic)
- Factory pattern for object creation
- Comprehensive debug infrastructure
- RAII memory management for embedded systems
- Platform-optimized for Teensy 4.x (ARM Cortex-M7, no RTTI)

**Last Updated:** 2026-03-31
**Current Phase:** Planning Phase - See [Implementation Plan](docs/plans/plan-impl-v01.md)

---

## TPS25751 Register Documentation

**IMPORTANT**: When working with TPS25751 registers, use the `tps25751-docs` MCP server for accurate register information from the TI Technical Reference Manual.

The MCP server provides:
- 27 complete register definitions with addresses and bit fields
- Enumerated value meanings for all fields
- Access types ([R], [R/W], [W]) for each bit field
- Code generation tools for Arduino/PlatformIO

**Available tools:** `search_register`, `explain_bitfield`, `generate_code_example`, `list_library_files`

**Detailed documentation:** See [docs/mcp-servers/tps25751-docs/AGENTS.md](docs/mcp-servers/tps25751-docs/AGENTS.md) for complete usage instructions and examples.

### Configuring the MCP Server Per Tool

The `tps25751-docs` MCP server uses the standard MCP protocol and works with any compatible AI coding assistant. Tool-specific config files are included in this repo:

| Tool | Config File | Notes |
|---|---|---|
| **Claude Code** | `.mcp.json` + `.claude/settings.json` | Auto-activated via `enabledMcpjsonServers` |
| **Cursor** | `.cursor/mcp.json` | Loaded automatically from project root |
| **VS Code (Copilot)** | `.vscode/mcp.json` | Requires GitHub Copilot with MCP support enabled |
| **Windsurf** | *(no project-level config)* | Add manually via Windsurf global MCP settings |

> **Note:** The `.claude/` directory contains Claude Code-specific settings (permissions, plugin config). Other tools use their own config conventions and are not affected by it.

---

## Engineering Standards

**CRITICAL:** Before implementing any register class or making code changes, review these engineering documents:

### Required Reading

1. **[Implementation Standards](docs/engineering/STANDARDS.md)** ⭐ **MOST IMPORTANT**
   - Mandatory class structure for all register implementations
   - Factory integration requirements
   - Three-tier validation system (basic, data, semantic)
   - Debug output format
   - Memory management rules
   - Anti-patterns to avoid

2. **[Technical Constraints](docs/engineering/CONSTRAINTS.md)** ⭐ **CRITICAL**
   - Platform-specific limitations (NO RTTI! Use `static_cast` not `dynamic_cast`)
   - I2C protocol details (TPS25751 custom protocol with length byte)
   - USB Power Delivery complexity (PDO/RDO parsing)
   - Template constraints (no `static_assert(false)`)
   - Memory considerations for embedded platform

3. **[Architecture](docs/engineering/ARCHITECTURE.md)**
   - System design and component relationships
   - Design patterns (Factory, Template Method, Singleton, RAII)
   - Data flow diagrams
   - Extension points for new registers

4. **[Testing Requirements](docs/engineering/TESTING.md)**
   - Unit testing structure and requirements
   - >90% code coverage requirement
   - Integration testing patterns
   - Hardware validation procedures

5. **[Documentation Requirements](docs/engineering/DOCUMENTATION.md)**
   - Doxygen documentation standards
   - Example sketch requirements
   - Troubleshooting guide format

6. **[Code Review Guidelines](docs/engineering/CODE_REVIEW_GUIDELINES.md)**
   - Platform-specific correctness checks (RTTI, `F()` macro, I2C length byte)
   - Embedded C++ bug patterns (bitfield shifts, PDO unit mismatch, integer overflow)
   - Standards compliance verification checklist
   - Testing coverage requirements for review sign-off

### Quick Reference: Register Implementation Checklist

Before implementing a new register class, ensure:

- [ ] Read [STANDARDS.md](docs/engineering/STANDARDS.md) for class structure
- [ ] Read [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) for platform limitations
- [ ] Understand I2C protocol (first byte is LENGTH, not data!)
- [ ] Use `static_cast`, NEVER `dynamic_cast` (no RTTI on platform)
- [ ] Implement three-tier validation (basic, data, semantic)
- [ ] Add to factory enum and implement creation methods
- [ ] Follow debug print format exactly
- [ ] Write unit tests with >90% coverage
- [ ] Document with Doxygen following [DOCUMENTATION.md](docs/engineering/DOCUMENTATION.md)

### Common Platform Gotchas

**❌ NEVER DO:**
```cpp
auto* status = dynamic_cast<TPS25751Status*>(ptr);  // NO RTTI!
if (typeid(*obj) == typeid(TPS25751Status)) { }     // NO RTTI!
template<typename T> void foo() { static_assert(false, "msg"); }  // Template error!
```

**✅ ALWAYS DO:**
```cpp
auto* status = static_cast<TPS25751Status*>(ptr);   // Use static_cast
RegisterType type = getRegisterType(addr);           // Use factory pattern
Serial.println(F("String in flash"));                // Use F() macro
```

---

## Implementation Status

**Implemented Registers:** 15/27
- STATUS, MODE, POWER_PATH_STATUS, BOOT_FLAGS, PORT_CONFIG
- TYPEC_STATE, INTERRUPT_EVENT, POWER_STATUS, PD_STATUS
- ACTIVE_PDO_CONTRACT, ACTIVE_RDO_CONTRACT, PORT_CONTROL
- GPIO_STATUS, RECEIVED_SINK_CAPS, RECEIVED_SOURCE_CAPS

**In Progress:** Planning remaining 12 registers
- See [Implementation Plan v0.1](docs/plans/plan-impl-v01.md) for prioritized roadmap

---

## Library Architecture

### Core Components

- **`TPS25751`**: Main controller class that handles I2C communication
- **`TPS25751Register`**: Abstract base class providing common functionality for all registers
- **`TPS25751BitUtils`**: Utility class for bitfield extraction from packed register data
- **`TPS25751RegisterFactory`**: Abstract factory interface for creating register objects
- **`TPS25751RegisterFactoryImpl`**: Concrete factory implementation
- **`TPS25751Factory`**: Singleton pattern for global factory access

### Register Classes

Each register has its own class inheriting from `TPS25751Register`:
- `TPS25751Status`: Connection state, VBUS status, power role
- `TPS25751Mode`: Current operating mode and capabilities
- `TPS25751PowerPathStatus`: Power path and charging status
- `TPS25751BootFlags`: Boot and initialization flags
- `TPS25751PortConfig`: Port configuration settings
- `TPS25751TypeCState`: Type-C state machine status
- `TPS25751InterruptEvent`: Interrupt event flags
- `TPS25751PowerStatus`: Power delivery status
- `TPS25751PDStatus`: PD contract status
- `TPS25751ActivePDOContract`: Active PDO contract details
- `TPS25751ActiveRDOContract`: Active RDO contract details
- `TPS25751PortControl`: Port control settings
- `TPS25751GPIOStatus`: GPIO pin status
- `TPS25751ReceivedSinkCaps`: Received sink capabilities
- `TPS25751ReceivedSourceCaps`: Received source capabilities

### Key Design Patterns

- **Base Register Class**: `TPS25751Register` provides common functionality for bitfield extraction and debug printing
- **Bitfield Utilities**: `TPS25751BitUtils` provides helper functions for extracting multi-bit fields from packed register data
- **I2C Protocol Handling**: The TPS25751 uses a custom I2C protocol where responses are prefixed with byte count
- **Factory Method Pattern**: Modern object creation using `TPS25751RegisterFactory`
- **Validation Layer**: Multi-level validation (basic, data, semantic) across all register classes
- **Debug Infrastructure**: Comprehensive logging system with categories and verbosity levels

---

## Architectural Improvements (2025)

### Factory Method Pattern Implementation

The library includes a complete Factory Method Pattern implementation:

- **`TPS25751RegisterFactory`**: Abstract factory interface for creating register objects
- **`TPS25751RegisterFactoryImpl`**: Concrete implementation supporting all register types
- **`TPS25751Factory`**: Singleton pattern for global factory access with injection support

### Enhanced Register Architecture

- **Validation Layer**: Three-tier validation system (basic, data, semantic) with public access
- **Debug Infrastructure**: Multi-level logging with categories (`DEBUG_CAT_I2C`, `DEBUG_CAT_REGISTER`, etc.)
- **String Handling**: Optimized compile-time string validation in Mode class
- **Memory Management**: Full RAII implementation with Rule of Five in base register class

### Platform-Specific Considerations

- **No RTTI**: Platform compiles with `-fno-rtti`, requiring `static_cast` instead of `dynamic_cast`
- **Template Constraints**: Use SFINAE patterns instead of `static_assert(false)` to avoid compilation issues
- **Memory Constraints**: Careful use of `std::unique_ptr` and move semantics for embedded efficiency

### Usage Patterns

```cpp
// Factory-based creation (preferred)
auto status = TPS25751Factory::getInstance().createRegister(RegisterType::STATUS);

// Integrated TPS25751 methods
auto statusReg = tps.readStatusRegister(true);  // with validation
```

---

## Architectural Decision Records (ADRs)

### ADR-001: Factory Pattern over Builder Pattern
**Status:** Accepted
**Date:** 2025-01
**Context:** Builder pattern deprecated
**Decision:** Prefer factory-based creation for all new code
**Consequences:**
- All new register classes use factory pattern
- `TPS25751RegisterBuilder` is deprecated; new code should not use it
- See [STANDARDS.md](docs/engineering/STANDARDS.md) for factory usage patterns

### ADR-002: Three-Tier Validation
**Status:** Accepted
**Date:** 2025-01
**Context:** Need comprehensive data validation
**Decision:** All registers implement basic, data, and semantic validation
**Consequences:**
- Validation can be run at different levels based on performance needs
- Semantic validation catches logical inconsistencies
- See [STANDARDS.md](docs/engineering/STANDARDS.md#validation-requirements)

### ADR-003: No RTTI Platform Compilation
**Status:** Accepted
**Date:** 2025-01
**Context:** Platform compiles with `-fno-rtti` for size optimization
**Decision:** Use static_cast and factory pattern instead of dynamic_cast
**Consequences:**
- Cannot use dynamic_cast or typeid
- Factory pattern provides type safety
- See [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md#no-rtti-run-time-type-information)

---

## Library Documentation Structure

```
/
├── AGENTS.md                           # This file - Library developer guide
├── .mcp.json                           # Claude Code MCP server config
├── .claude/                            # Claude Code-specific settings
├── .cursor/mcp.json                    # Cursor MCP server config
├── .vscode/mcp.json                    # VS Code Copilot MCP server config
├── library.json                        # PlatformIO library manifest
├── library.properties                  # Arduino library manifest
├── include/                            # Public header files
├── src/                                # Implementation files
├── examples/                           # Example sketches
└── docs/
    ├── AGENTS.md                       # MCP server documentation
    ├── engineering/                    # Engineering standards (REQUIRED READING)
    │   ├── STANDARDS.md                # Implementation standards
    │   ├── CONSTRAINTS.md              # Platform constraints
    │   ├── ARCHITECTURE.md             # System design
    │   ├── TESTING.md                  # Testing requirements
    │   ├── DOCUMENTATION.md            # Documentation standards
    │   └── CODE_REVIEW_GUIDELINES.md   # Review checklist (embedded C++ / TPS25751-specific)
    ├── plans/
    │   └── plan-impl-v01.md            # Implementation plan (12 registers remaining)
    └── mcp-servers/
        └── tps25751-docs/              # MCP server for register documentation
            └── AGENTS.md               # MCP server usage guide
```

**Navigation:**
- **Getting Started:** Read this file first
- **Understanding the System:** See [ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md)
- **Implementing Registers:** Start with [STANDARDS.md](docs/engineering/STANDARDS.md)
- **Platform Issues:** See [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md)
- **Writing Tests:** See [TESTING.md](docs/engineering/TESTING.md)
- **Reviewing Code:** See [CODE_REVIEW_GUIDELINES.md](docs/engineering/CODE_REVIEW_GUIDELINES.md)
- **Current Roadmap:** See [Implementation Plan](docs/plans/plan-impl-v01.md)
- **MCP Server:** See [docs/mcp-servers/tps25751-docs/AGENTS.md](docs/mcp-servers/tps25751-docs/AGENTS.md)

---

## Known Issues

### Library-Specific Issues

1. **Platform Constraints**
   - All dynamic_cast usage has been replaced with static_cast due to -fno-rtti compilation flag
   - Template functions must avoid static_assert(false) patterns that cause premature template instantiation
   - See [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) for complete list

---

## Quick Tips for AI Assistants

### When Implementing a New Register Class

1. **First:** Read existing register classes (e.g., `TPS25751Status.h`) to understand the architecture
2. **Second:** Read [STANDARDS.md](docs/engineering/STANDARDS.md) - It has the complete template
3. **Third:** Check [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) for platform gotchas
4. **Use the MCP servers:**
   - `tps25751-docs` (`search_register`, `explain_bitfield`) for register information
5. **Follow the checklist:** Every register needs:
   - Class inheriting from `TPS25751Register`
   - Factory integration (enum, forward declaration, creation methods)
   - Three validation levels (basic, data, semantic)
   - Debug print following format
   - Unit tests with >90% coverage
   - Doxygen documentation
5. **Test on hardware:** If available, validate with actual TPS25751

### Common Mistakes to Avoid

- ❌ Using `dynamic_cast` (use `static_cast`)
- ❌ Using `typeid` (use factory pattern)
- ❌ Forgetting I2C length byte (first byte is length, not data!)
- ❌ Wrong PDO unit conversion (PPS uses different units!)
- ❌ String literals without `F()` macro (wastes RAM)
- ❌ `static_assert(false)` in templates (use dependent type)

### When You Get Stuck

1. Check [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) - Most issues are platform-specific
2. Look at existing register implementations as examples (STATUS, MODE, etc.)
3. Use the `tps25751-docs` MCP server for register details
4. Reference [Implementation Plan](docs/plans/plan-impl-v01.md) for detailed requirements
5. Check TI TPS25751 Technical Reference Manual for register specifications

---

## Development Workflow

### Adding a New Register Class

1. **Analyze**: Read existing register classes (e.g., `TPS25751Status.h/.cpp`) to understand patterns
2. **Research**: Use `mcp__tps25751-docs__search_register` tool to find register details from TI docs
3. **Plan**: Review [STANDARDS.md](docs/engineering/STANDARDS.md) template
4. **Implement**: Create class following checklist above
5. **Factory**: Add to `RegisterType` enum and factory implementation
6. **Test**: Write unit tests achieving >90% coverage
7. **Document**: Add Doxygen comments following [DOCUMENTATION.md](docs/engineering/DOCUMENTATION.md)
8. **Validate**: Test on hardware if available

### Testing Your Changes

```bash
# From project root
pio test                  # Run unit tests
pio test --coverage       # Run with coverage report
```

### Publishing the Library

The library is currently maintained as a local library. Future plans include:
- Publishing to PlatformIO Library Registry
- Standalone repository with CI/CD
- Additional platform support (ESP32, RP2040)

---
