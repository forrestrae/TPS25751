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

**Last Updated:** 2026-06-22
**Current Phase:** Active implementation — 17/27 registers done (COMMAND/DATA added,
enabling the 4CC command-task interface and I2Cc downstream-device proxy); see
[Implementation Plan](docs/plans/plan-impl-v01.md) for the remaining 10

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
- [ ] Add a factory `case` keyed on the register's `TPS25751Registers::Address` and implement creation methods
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
auto reg = factory.createRegister(addr);             // Use factory pattern (by Address)
Serial.println(F("String in flash"));                // Use F() macro
```

---

## Implementation Status

**Implemented Registers:** 17/27
- STATUS, MODE, POWER_PATH_STATUS, BOOT_FLAGS, PORT_CONFIG
- TYPEC_STATE, INTERRUPT_EVENT, POWER_STATUS, PD_STATUS
- ACTIVE_PDO_CONTRACT, ACTIVE_RDO_CONTRACT, PORT_CONTROL
- GPIO_STATUS, RECEIVED_SINK_CAPS, RECEIVED_SOURCE_CAPS
- COMMAND, DATA — the 4CC command-task control/payload registers (TRM Sec 4.4.2);
  see `TPS25751::executeCommand()` and `TPS25751DownstreamDevice`

**In Progress:** Planning remaining 10 registers
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
- **`TPS25751DownstreamDevice`**: Proxies register access to a device on the
  TPS25751's secondary I2Cc bus (e.g. a BQ25798 charger) via the I2Cr/I2Cw 4CC
  command-tasks; composes a `TPS25751` host rather than subclassing it

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
- `TPS25751Command`: COMMAND register (0x08) — 4CC task code, clear/rejected state
- `TPS25751Data`: DATA register (0x09) — generic 64-byte 4CC task payload (no fixed layout)

### 4CC Command-Task Interface & I2Cc Downstream-Device Proxy

The TPS25751 reaches devices on its secondary I2Cc bus (e.g. a BQ25798 charger)
through a generic 4CC command-task interface: write input bytes to DATA, write a
4-character ASCII task code to COMMAND, the controller runs the task, then clears
COMMAND (success) or writes `"!CMD"` (rejected); results are read back from DATA.

- **`TPS25751::executeCommand(cmd, inData, inLen, outData, outLen, timeoutMs)`**:
  generic, task-agnostic executor — handles the DATA write → COMMAND write →
  poll → DATA read handshake.
- **`TPS25751FourCC` / `TPS25751TaskStatus` / `TPS25751TaskResult`**
  (`include/TPS25751Task.h`): value types for the 4CC code and the executor's
  result (`Success` / `Rejected` / `Timeout` / `I2CError`).
- **`TPS25751DownstreamDevice`**: the I2Cc analog of `TPS25751` — encodes/decodes
  the I2Cr (read) and I2Cw (write) task DATA payloads, including the 7-bit target
  address masking, the read-data-starts-at-DATA-offset-1 convention, the 11-byte
  I2Cw payload cap, the I2Cw `Length`-counts-the-offset-byte rule (`payload_len + 1`),
  and the TRM's 5-second minimum spacing between consecutive same-type commands
  (warns via `DEBUG_CAT_TASK`, does not hard-block). I2Cr/I2Cw layouts verified on
  hardware against a BQ25798.

See [docs/engineering/ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md) (ADR-007)
and [docs/engineering/CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) ("4CC Command
Interface & I2Cc Downstream-Device Proxy") for full details.

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

See [Common Platform Gotchas](#common-platform-gotchas), ADR-003, and
[CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) — no RTTI (`static_cast` only),
SFINAE over `static_assert(false)`, and RAII/`unique_ptr` memory management.

### Usage Patterns

```cpp
// Factory-based creation (preferred) — registers are identified by Address
auto status = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::STATUS);

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
│                                       #   (CLAUDE.md is a symlink -> AGENTS.md; edit AGENTS.md)
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
6. **Test on hardware:** If available, validate with actual TPS25751

### Common Mistakes to Avoid

See [Common Platform Gotchas](#common-platform-gotchas) for the C++/platform
rules (no `dynamic_cast`/`typeid`, use `F()`, no `static_assert(false)`).
TPS25751-specific traps to add:

- ❌ Forgetting the I2C length byte (first byte is length, not data!)
- ❌ Wrong PDO unit conversion (PPS uses different units!)
- ❌ Reading I2Cr results from `DATA[0]` instead of `DATA[1]` (`DATA[0]` is
  the task return code, not the first byte of the downstream register read)
- ❌ Setting the I2Cw `Length` field to just the payload length — it must count
  the register-offset byte too (`payload_len + 1`). The controller transmits
  exactly `Length` bytes starting at the offset byte, so `Length == payload_len`
  sends only the offset (a pointer-only write: ACKed, no NACK, register unchanged)
- ❌ Assuming I2Cw mirrors I2Cr — it doesn't. I2Cr is `{addr, regOffset, numBytes}`;
  I2Cw is `{addr, Length, regOffset, payload}` (length/offset swapped, Length counts
  the offset byte)
- ❌ Exceeding the I2Cw 11-byte payload cap (bytes beyond DATA offset 14 are
  silently dropped by the controller, not rejected — always bounds-check
  `len <= 11` before calling)
- ❌ Requesting more than 63 bytes in one I2Cr (the 64-byte DATA register holds
  the return code at `DATA[0]`, leaving 63 for read data)
- ❌ Passing a raw 8-bit I2C address (with an R/W bit baked in) as the I2Cr/
  I2Cw target address — bit 7 is reserved and must be masked to 7 bits
- ❌ Assuming `TPS25751::executeCommand()` enforces the TRM's 5-second
  minimum spacing between consecutive I2Cr (or consecutive I2Cw) commands —
  it doesn't; only `TPS25751DownstreamDevice` tracks and warns about this

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
5. **Factory**: Add a `case` for the register's `TPS25751Registers::Address` to the factory's `createRegister` switches (no separate type enum — `Address` is the single register identity)
6. **Test**: Write unit tests achieving >90% coverage
7. **Document**: Add Doxygen comments following [DOCUMENTATION.md](docs/engineering/DOCUMENTATION.md)
8. **Validate**: Test on hardware if available

### Building & Testing Your Changes

This directory is a **library**, not a standalone PlatformIO project — there is no
root `platformio.ini`, and the bundled example does **not** build on its own (its
`lib_deps = lib/TPS25751` only resolves from a project that actually contains
`lib/TPS25751`).

In this repo the library lives at `lib/TPS25751` inside the parent PlatformIO
project (`TPS25751-i2ct-test`, two levels up), which symlinks the example's
`platformio.ini`, `src/`, and `include/` and provides `lib/TPS25751`. Build from
**that parent project root**:

```bash
# From this library root, the parent project is two levels up:
cd ../..                  # -> TPS25751-i2ct-test (has platformio.ini -> example)
pio run                   # Compile for Teensy 4.0 (default_envs = teensy40)
pio run -t upload         # Flash to a connected Teensy
pio device monitor        # Serial monitor @ 115200
```

(When consuming this as a published library, copy the example out into your own
PlatformIO project and add this library under `lib/`.)

> **No unit tests exist yet.** `test/` is a PlatformIO placeholder (README only).
> The `>90% coverage` target in [TESTING.md](docs/engineering/TESTING.md) and a
> `pio test` workflow are the *intended* setup, not a working one — adding the
> test harness is still pending.

### Publishing the Library

The library is currently maintained as a local library. Future plans include:
- Publishing to PlatformIO Library Registry
- Standalone repository with CI/CD
- Additional platform support (ESP32, RP2040)

---
