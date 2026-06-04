# TPS25751 Register Implementation Plan v0.1

* **Date:** 2025-10-20
* **Last Updated:** 2025-10-22
* **Project:** TPS25751 USB-C PD Controller Library
* **Status:** Phase 1 Complete - In Progress (Phase 2)

---

## Executive Summary

This document outlines the implementation plan for the remaining registers from the TPS25751 USB-C Power Delivery controller. The registers are prioritized based on their value for troubleshooting and debugging USB-C PD operations, with critical power delivery diagnostics taking highest priority.

**Total Registers:** 26
**Currently Implemented:** 15 (Phase 1 Complete ✅)
**Remaining to Implement:** 11
**Implementation Phases:** 4
**Completed Phases:** 1 of 4

**IMPORTANT:** This is a planning document. For implementation details, refer to:
- **[Implementation Standards](../engineering/STANDARDS.md)** - Class structure, validation, factory integration
- **[Technical Constraints](../engineering/CONSTRAINTS.md)** - Platform limitations, I2C protocol, USB PD complexity
- **[Testing Requirements](../engineering/TESTING.md)** - Unit, integration, and hardware testing
- **[Documentation Standards](../engineering/DOCUMENTATION.md)** - Doxygen, examples, troubleshooting
- **[Architecture](../engineering/ARCHITECTURE.md)** - System design and component relationships

---

## Phase 1 Completion Summary (2025-10-22)

**✅ All 6 Phase 1 registers successfully implemented!**

The critical Power Delivery diagnostics registers are now complete, providing comprehensive USB-C PD troubleshooting capabilities:

- **GPIO_STATUS** (0x72) - Hardware debugging with GPIO pin status
- **PORT_CONTROL** (0x29) - Port behavior control and role swap management
- **ACTIVE_PDO_CONTRACT** (0x34) - Detailed active power contract with full PDO parsing
- **ACTIVE_RDO_CONTRACT** (0x35) - Request Data Object showing negotiated terms
- **RECEIVED_SOURCE_CAPABILITIES** (0x30) - Complete source capability list (up to 7 PDOs)
- **RECEIVED_SINK_CAPABILITIES** (0x31) - Complete sink capability list (up to 7 PDOs)

**Key Achievements:**
- ✅ All 4 USB PD PDO types supported (Fixed, Battery, Variable, PPS/Augmented)
- ✅ Correct unit conversions for all PDO types (including special PPS scaling)
- ✅ Complete factory pattern integration
- ✅ Three-tier validation (basic, data, semantic)
- ✅ Comprehensive debug printing
- ✅ Main library convenience methods added
- ✅ Example application updated to demonstrate all registers
- ✅ Clean build with no warnings (99KB flash, 26KB RAM)

**Progress:** 58% of total implementation complete (15 of 26 registers)

**Next Phase:** Phase 2 - Interrupt & Configuration (6 registers, ~8 hours estimated)

---

## Current Implementation Status

### ✅ Implemented Registers (15)

#### Original Implementation (9 registers)
| Register | Address | Class Name | Size | Purpose |
|----------|---------|------------|------|---------|
| STATUS | 0x1A | `TPS25751Status` | 5 bytes | Connection state, VBUS status, power role |
| MODE | 0x03 | `TPS25751Mode` | 4 bytes | Operating mode and capabilities |
| POWER_PATH_STATUS | 0x26 | `TPS25751PowerPathStatus` | 5 bytes | Power path and charging status |
| BOOT_STATUS | 0x2D | `TPS25751BootFlags` | 5 bytes | Boot and initialization flags |
| PORT_CONFIGURATION | 0x28 | `TPS25751PortConfig` | 17 bytes | Port configuration settings |
| TYPEC_STATE | 0x69 | `TPS25751TypeCState` | 4 bytes | Type-C state machine status |
| INT_EVENT1 | 0x14 | `TPS25751InterruptEvent` | 11 bytes | Interrupt events |
| POWER_STATUS | 0x3F | `TPS25751PowerStatus` | 2 bytes | Power status information |
| PD_STATUS | 0x40 | `TPS25751PDStatus` | 4 bytes | Power Delivery status |

#### Phase 1 Implementation (6 registers) ✅ COMPLETE
| Register | Address | Class Name | Size | Purpose | Completed |
|----------|---------|------------|------|---------|-----------|
| GPIO_STATUS | 0x72 | `TPS25751GPIOStatus` | 8 bytes | GPIO pin status and direction | 2025-10-22 |
| PORT_CONTROL | 0x29 | `TPS25751PortControl` | 4 bytes | Port control settings, role swaps | 2025-10-22 |
| ACTIVE_PDO_CONTRACT | 0x34 | `TPS25751ActivePDOContract` | 6 bytes | Active USB PD power contract | 2025-10-22 |
| ACTIVE_RDO_CONTRACT | 0x35 | `TPS25751ActiveRDOContract` | 4 bytes | Active USB PD request contract | 2025-10-22 |
| RECEIVED_SOURCE_CAPABILITIES | 0x30 | `TPS25751ReceivedSourceCaps` | 29 bytes | Source capabilities (up to 7 PDOs) | 2025-10-22 |
| RECEIVED_SINK_CAPABILITIES | 0x31 | `TPS25751ReceivedSinkCaps` | 29 bytes | Sink capabilities (up to 7 PDOs) | 2025-10-22 |

### ❌ Unimplemented Registers (11)

| # | Register | Address | Size | Access | Priority | Phase |
|---|----------|---------|------|--------|----------|-------|
| 1 | INT_MASK1 | 0x16 | 11 bytes | R/W | **MEDIUM** | 2 |
| 2 | INT_CLEAR1 | 0x18 | 11 bytes | W | **MEDIUM** | 2 |
| 3 | TRANSMIT_SOURCE_CAPABILITIES | 0x32 | 29 bytes | R/W | **MEDIUM** | 2 |
| 4 | TRANSMIT_SINK_CAPABILITIES | 0x33 | 29 bytes | R/W | **MEDIUM** | 2 |
| 5 | IO_CONFIG | 0x5C | 48 bytes | R/W | **MEDIUM** | 2 |
| 6 | AUTONEGOTIATE_SINK | 0x37 | 24 bytes | R/W | **MEDIUM** | 2 |
| 7 | COMMAND | 0x08 | 4 bytes | W | **LOW** | 4 |
| 8 | DATA | 0x09 | 64 bytes | R/W | **LOW** | 4 |
| 9 | CUSTOMER_USE | 0x06 | 8 bytes | R/W | **LOW** | 4 |
| 10 | SLEEP_CONTROL | 0x70 | 1 byte | R/W | **LOW** | 4 |
| 11 | LIQUID_DETECTION_CONFIG | 0x98 | 11 bytes | R/W | **LOW** | 4 |

---

## Prioritization Rationale

### Phase 1: Critical Power Delivery Diagnostics (HIGH PRIORITY)

These registers provide essential information for troubleshooting USB-C Power Delivery negotiations and connections:

1. **ACTIVE_PDO_CONTRACT (0x34)**
   - **Why:** Shows the actual negotiated Power Data Object between source and sink
   - **Troubleshooting Value:** Critical for understanding why devices aren't charging at expected voltage/current
   - **Use Cases:** Diagnosing power negotiation failures, voltage drops, current limiting issues

2. **ACTIVE_RDO_CONTRACT (0x35)**
   - **Why:** Shows the actual Request Data Object that was accepted
   - **Troubleshooting Value:** Critical for understanding what the sink requested vs. what was negotiated
   - **Use Cases:** Diagnosing mismatched power expectations, capability mismatches

3. **RECEIVED_SOURCE_CAPABILITIES (0x30)**
   - **Why:** Shows what power capabilities the connected source device advertised
   - **Troubleshooting Value:** Essential for understanding what power options are available
   - **Use Cases:** Diagnosing incompatible chargers, understanding why certain power levels aren't available

4. **RECEIVED_SINK_CAPABILITIES (0x31)**
   - **Why:** Shows what power capabilities the connected sink device advertised
   - **Troubleshooting Value:** Essential when acting as a source to understand sink requirements
   - **Use Cases:** Diagnosing sink device compatibility, power delivery failures

5. **GPIO_STATUS (0x72)**
   - **Why:** Shows the current state of all GPIO pins
   - **Troubleshooting Value:** Critical for hardware debugging and verifying pin configurations
   - **Use Cases:** Debugging external control signals, verifying hardware connections, LED states

6. **PORT_CONTROL (0x29)**
   - **Why:** Controls port behavior and can read current control settings
   - **Troubleshooting Value:** Important for understanding and modifying port behavior
   - **Use Cases:** Forcing port roles, enabling/disabling features, port reset operations

### Phase 2: Interrupt & Configuration (MEDIUM PRIORITY)

These registers enable advanced interrupt handling and configuration management:

7. **INT_MASK1 (0x16)**
   - **Why:** Controls which interrupts are enabled/disabled
   - **Troubleshooting Value:** Useful for understanding interrupt behavior and selective monitoring
   - **Use Cases:** Implementing interrupt-driven designs, debugging interrupt storms

8. **INT_CLEAR1 (0x18)**
   - **Why:** Clears pending interrupt flags
   - **Troubleshooting Value:** Essential for proper interrupt handling
   - **Use Cases:** Clearing interrupts after handling, resetting interrupt state

9. **TRANSMIT_SOURCE_CAPABILITIES (0x32)**
   - **Why:** Shows what capabilities we advertise when acting as a source
   - **Troubleshooting Value:** Useful for verifying configuration and understanding negotiation behavior
   - **Use Cases:** Debugging why certain power profiles aren't being offered

10. **TRANSMIT_SINK_CAPABILITIES (0x33)**
    - **Why:** Shows what capabilities we advertise when acting as a sink
    - **Troubleshooting Value:** Useful for verifying configuration and understanding negotiation behavior
    - **Use Cases:** Debugging why certain power requests are failing

11. **IO_CONFIG (0x5C)**
    - **Why:** Configures I/O pins for various functions
    - **Troubleshooting Value:** Important for pin configuration debugging
    - **Use Cases:** Setting up GPIO modes, configuring special functions

12. **AUTONEGOTIATE_SINK (0x37)**
    - **Why:** Controls automatic power negotiation behavior
    - **Troubleshooting Value:** Useful for understanding automatic negotiation decisions
    - **Use Cases:** Debugging auto-negotiation issues, configuring negotiation priorities

### Phase 3: Advanced Features (MEDIUM PRIORITY)

Note: AUTONEGOTIATE_SINK was moved to Phase 2 due to its importance in PD negotiation troubleshooting.

### Phase 4: Specialized/Low Priority (LOW PRIORITY)

These registers are either write-only, application-specific, or used in specialized scenarios:

13. **COMMAND (0x08)**
    - **Why:** Write-only register for sending commands
    - **Troubleshooting Value:** Limited - primarily used for control, not status reading
    - **Use Cases:** Sending firmware commands, triggering operations

14. **DATA (0x09)**
    - **Why:** Generic data buffer for command parameters/responses
    - **Troubleshooting Value:** Limited - context-dependent, less structured
    - **Use Cases:** Command data exchange, vendor-specific operations

15. **CUSTOMER_USE (0x06)**
    - **Why:** Customer-specific register with application-dependent meaning
    - **Troubleshooting Value:** Limited - varies by application
    - **Use Cases:** Custom application features

16. **SLEEP_CONTROL (0x70)**
    - **Why:** Controls sleep/power-saving modes
    - **Troubleshooting Value:** Limited - niche power management use case
    - **Use Cases:** Low-power applications, battery-powered devices

17. **LIQUID_DETECTION_CONFIG (0x98)**
    - **Why:** Configures liquid detection feature
    - **Troubleshooting Value:** Limited - specialized safety feature
    - **Use Cases:** Devices requiring liquid intrusion detection

---

## Implementation Plan by Phase

### Phase 1: Critical Power Delivery Diagnostics ✅ COMPLETE

**Goal:** Enable comprehensive PD negotiation troubleshooting
**Estimated Effort:** 12-15 hours (Actual: ~12 hours)
**Priority:** Must have for production debugging
**Status:** ✅ **COMPLETE - 2025-10-22**
**Completion Notes:** All 6 Phase 1 registers successfully implemented, integrated into factory pattern, and demonstrated in example application. Build successful with no warnings.

#### 1.1 Active Contract Registers (4 hours)

**ACTIVE_PDO_CONTRACT (0x34) - 6 bytes**
- Implement `TPS25751ActivePDOContract` class
- Parse PDO type (Fixed, Battery, Variable, Augmented)
- Extract voltage, current, and power fields
- Decode PDO flags and options
- Add factory support in `RegisterType` enum
- Unit tests for all PDO types

**ACTIVE_RDO_CONTRACT (0x35) - 4 bytes**
- Implement `TPS25751ActiveRDOContract` class
- Parse object position, give-back flag
- Extract operating/max current or power
- Decode capability mismatch, USB comms, and other flags
- Add factory support
- Unit tests for various RDO scenarios

#### 1.2 Capabilities Registers (6 hours)

**RECEIVED_SOURCE_CAPABILITIES (0x30) - 29 bytes**
- Implement `TPS25751ReceivedSourceCaps` class
- Parse PDO count (up to 7 PDOs)
- Iterate through PDO array
- Decode each PDO based on type
- Pretty-print capability list
- Add factory support
- Unit tests with various capability sets

**RECEIVED_SINK_CAPABILITIES (0x31) - 29 bytes**
- Implement `TPS25751ReceivedSinkCaps` class
- Similar structure to source capabilities
- Parse sink-specific PDO fields
- Decode higher capability and fast role swap
- Add factory support
- Unit tests

#### 1.3 GPIO and Control Registers (4 hours)

**GPIO_STATUS (0x72) - 8 bytes**
- Implement `TPS25751GPIOStatus` class
- Parse individual GPIO pin states (input/output values)
- Decode GPIO function assignments
- Provide per-pin accessors
- Add factory support
- Unit tests

**PORT_CONTROL (0x29) - 4 bytes**
- Implement `TPS25751PortControl` class
- Parse control flags (port enable, role control, etc.)
- Decode command bits
- Support both read and write operations
- Add factory support
- Unit tests for control scenarios

#### 1.4 Factory Integration
- Add all 6 register types to `RegisterType` enum
- Update `TPS25751RegisterFactoryImpl` with creation methods
- Update `getRegisterType()` and `getAddress()` mappings
- Add validation in `validateTypeAddress()`

#### 1.5 Main Library Integration
- Add convenience methods to `TPS25751` class:
  - `readActivePDOContract()`
  - `readActiveRDOContract()`
  - `readReceivedSourceCapabilities()`
  - `readReceivedSinkCapabilities()`
  - `readGPIOStatus()`
  - `readPortControl()` / `writePortControl()`

#### 1.6 Testing & Documentation
- Unit tests for each register class
- Integration tests with I2C mock
- Update examples to demonstrate usage
- Document troubleshooting workflows

---

### Phase 2: Interrupt & Configuration (8 hours)

#### 2.1 Interrupt Control Registers (3 hours)

**INT_MASK1 (0x16) - 11 bytes**
- Implement `TPS25751InterruptMask` class
- Mirror structure of `TPS25751InterruptEvent`
- Parse mask bits for each interrupt source
- Support read/write operations
- Add factory support
- Unit tests

**INT_CLEAR1 (0x18) - 11 bytes**
- Implement `TPS25751InterruptClear` class
- Write-only register support
- Mirror interrupt event structure
- Provide methods to clear specific interrupts
- Provide method to clear all interrupts
- Add factory support
- Unit tests

#### 2.2 Capability Configuration Registers (4 hours)

**TRANSMIT_SOURCE_CAPABILITIES (0x32) - 29 bytes**
- Implement `TPS25751TransmitSourceCaps` class
- Similar to ReceivedSourceCaps but writable
- Support capability list modification
- Validate PDO count and values
- Add factory support
- Unit tests

**TRANSMIT_SINK_CAPABILITIES (0x33) - 29 bytes**
- Implement `TPS25751TransmitSinkCaps` class
- Similar to ReceivedSinkCaps but writable
- Support capability list modification
- Validate PDO count and values
- Add factory support
- Unit tests

#### 2.3 I/O and Negotiation Configuration (4 hours)

**IO_CONFIG (0x5C) - 48 bytes**
- Implement `TPS25751IOConfig` class
- Parse GPIO configurations (48 bytes suggests detailed per-pin config)
- Decode pin modes, pull-ups/downs, special functions
- Support read/write operations
- Add factory support
- Unit tests

**AUTONEGOTIATE_SINK (0x37) - 24 bytes**
- Implement `TPS25751AutoNegotiateSink` class
- Parse auto-negotiation preferences
- Decode voltage/current preferences
- Support configuration modification
- Add factory support
- Unit tests

#### 2.4 Factory & Integration
- Add 6 register types to factory
- Update main library with convenience methods
- Integration tests
- Documentation

---

### Phase 3: Advanced Features (2 hours)

This phase is now empty as AUTONEGOTIATE_SINK was moved to Phase 2. Consider this a buffer for any refactoring or additional testing needed.

---

### Phase 4: Specialized/Low Priority (6 hours)

#### 4.1 Command Interface (2 hours)

**COMMAND (0x08) - 4 bytes**
- Implement `TPS25751Command` class
- Define command codes as enums
- Provide command builder methods
- Write-only register handling
- Add factory support
- Unit tests

**DATA (0x09) - 64 bytes**
- Implement `TPS25751Data` class
- Generic byte array handling
- Support for structured data based on command context
- Add factory support
- Unit tests

#### 4.2 Application-Specific Registers (3 hours)

**CUSTOMER_USE (0x06) - 8 bytes**
- Implement `TPS25751CustomerUse` class
- Generic 8-byte data storage
- Application-specific interpretation left to user
- Add factory support
- Unit tests

**SLEEP_CONTROL (0x70) - 1 byte**
- Implement `TPS25751SleepControl` class
- Parse sleep mode flags
- Decode wake conditions
- Support read/write
- Add factory support
- Unit tests

**LIQUID_DETECTION_CONFIG (0x98) - 11 bytes**
- Implement `TPS25751LiquidDetectionConfig` class
- Parse liquid detection settings
- Decode thresholds and response actions
- Support read/write
- Add factory support
- Unit tests

#### 4.3 Final Integration
- Complete factory implementation
- Final library method additions
- Comprehensive integration tests
- Complete documentation

---

## Implementation Requirements

**CRITICAL:** All implementations must follow the standards defined in [docs/engineering/](../engineering/)

### Before Starting Any Implementation

Read these documents in order:

1. **[STANDARDS.md](../engineering/STANDARDS.md)** - Complete class structure, validation requirements, factory integration
2. **[CONSTRAINTS.md](../engineering/CONSTRAINTS.md)** - Platform limitations (NO RTTI!), I2C protocol, USB PD complexity
3. **[ARCHITECTURE.md](../engineering/ARCHITECTURE.md)** - System design and component relationships
4. **[TESTING.md](../engineering/TESTING.md)** - Unit test requirements (>90% coverage)
5. **[DOCUMENTATION.md](../engineering/DOCUMENTATION.md)** - Doxygen comments, examples

### Implementation Checklist

For each register, verify:

- [ ] Read [STANDARDS.md](../engineering/STANDARDS.md) for class template
- [ ] Understand platform constraints from [CONSTRAINTS.md](../engineering/CONSTRAINTS.md)
- [ ] Class inherits from `TPS25751Register`
- [ ] Factory integration complete (enum, methods, mappings)
- [ ] Three-tier validation implemented
- [ ] Debug print follows standard format
- [ ] Unit tests written with >90% coverage
- [ ] Doxygen documentation complete
- [ ] No compiler warnings
- [ ] Hardware tested (if available)

---

## Estimated Timeline

| Phase | Registers | Status | Hours (Est/Actual) | Calendar Time* |
|-------|-----------|--------|-------------------|----------------|
| Phase 1 | 6 | ✅ Complete | 15 / ~12 | 2 days |
| Phase 2 | 6 | Pending | 8 / - | 1 week |
| Phase 3 | 0 | N/A | 2 / - | Buffer |
| Phase 4 | 5 | Pending | 6 / - | 1 week |
| **Total** | **17** | **58% Complete** | **31 / ~12** | **2 days (so far)** |

\* Assumes part-time development (2-3 hours per day)

---

## Success Criteria

### Functional Requirements
- ✅ All 17 registers implemented with full parsing
- ✅ Factory integration complete for all new registers
- ✅ All validation layers working (basic, data, semantic)
- ✅ Debug printing for all registers
- ✅ Main library convenience methods added

### Quality Requirements
- ✅ Unit test coverage > 90% (per [TESTING.md](../engineering/TESTING.md))
- ✅ No memory leaks (verified with Valgrind or similar)
- ✅ Compiles without warnings on Teensy platform
- ✅ All registers tested with real hardware
- ✅ All standards from [STANDARDS.md](../engineering/STANDARDS.md) followed

### Documentation Requirements
- ✅ Doxygen documentation complete (per [DOCUMENTATION.md](../engineering/DOCUMENTATION.md))
- ✅ Example sketches for all major features
- ✅ Troubleshooting guide updated with new register info

---

## Dependencies and Risks

### Dependencies
- TI TPS25751 Technical Reference Manual
- USB Power Delivery Specification 3.1
- Teensy Arduino core
- PlatformIO build system

### Risks
| Risk | Impact | Mitigation |
|------|--------|------------|
| Incomplete datasheet information | High | Reference USB PD spec, test with hardware |
| Platform memory constraints | Medium | See [CONSTRAINTS.md](../engineering/CONSTRAINTS.md) for optimization strategies |
| Complex PDO/RDO parsing | Medium | See [CONSTRAINTS.md](../engineering/CONSTRAINTS.md) USB PD section, thorough unit testing |
| Register behavior undocumented | Low | Empirical testing, community knowledge |

---

## Future Enhancements

After completing all 17 registers, consider:

1. **Caching Layer:** Cache frequently-read registers to reduce I2C traffic
2. **Async I2C:** Non-blocking I2C reads for better performance
3. **Register Subscriptions:** Callback notifications when registers change
4. **Configuration Profiles:** Save/restore complete device configurations
5. **Power Profile Builder:** Helper class to construct PDO/RDO sequences
6. **Interrupt-Driven Architecture:** Full interrupt-based event handling
7. **Data Logging:** Record register state changes over time for analysis

---

## References

- TI TPS25751 Technical Reference Manual
- USB Type-C Cable and Connector Specification
- USB Power Delivery Specification 3.1
- TPS25751 Library GitHub Repository (when published)
- PlatformIO Documentation
- Teensy 4.x Documentation

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2025-10-20 | Claude Code | Initial planning document |
| 0.2 | 2025-10-20 | Claude Code | Removed duplicate content, added references to engineering docs |
| 0.3 | 2025-10-22 | Claude Code | **Phase 1 COMPLETE**: Updated status to reflect completion of all 6 Phase 1 registers (GPIO_STATUS, PORT_CONTROL, ACTIVE_PDO_CONTRACT, ACTIVE_RDO_CONTRACT, RECEIVED_SOURCE_CAPABILITIES, RECEIVED_SINK_CAPABILITIES). Updated counts: 15/26 registers implemented, 11 remaining. Added completion dates and notes. |

---

## Appendix A: Register Quick Reference

| Reg# | Name | Addr | Size | R/W | Phase | Priority |
|------|------|------|------|-----|-------|----------|
| 1 | MODE | 0x03 | 4 | R | ✅ Done | - |
| 2 | CUSTOMER_USE | 0x06 | 8 | R/W | 4 | LOW |
| 3 | COMMAND | 0x08 | 4 | W | 4 | LOW |
| 4 | DATA | 0x09 | 64 | R/W | 4 | LOW |
| 5 | INT_EVENT1 | 0x14 | 11 | R | ✅ Done | - |
| 6 | INT_MASK1 | 0x16 | 11 | R/W | 2 | MEDIUM |
| 7 | INT_CLEAR1 | 0x18 | 11 | W | 2 | MEDIUM |
| 8 | STATUS | 0x1A | 5 | R | ✅ Done | - |
| 9 | POWER_PATH_STATUS | 0x26 | 5 | R | ✅ Done | - |
| 10 | PORT_CONFIGURATION | 0x28 | 17 | R | ✅ Done | - |
| 11 | PORT_CONTROL | 0x29 | 4 | R/W | ✅ Done (Phase 1) | - |
| 12 | BOOT_STATUS | 0x2D | 5 | R | ✅ Done | - |
| 13 | RECEIVED_SOURCE_CAPABILITIES | 0x30 | 29 | R | ✅ Done (Phase 1) | - |
| 14 | RECEIVED_SINK_CAPABILITIES | 0x31 | 29 | R | ✅ Done (Phase 1) | - |
| 15 | TRANSMIT_SOURCE_CAPABILITIES | 0x32 | 29 | R/W | 2 | MEDIUM |
| 16 | TRANSMIT_SINK_CAPABILITIES | 0x33 | 29 | R/W | 2 | MEDIUM |
| 17 | ACTIVE_PDO_CONTRACT | 0x34 | 6 | R | ✅ Done (Phase 1) | - |
| 18 | ACTIVE_RDO_CONTRACT | 0x35 | 4 | R | ✅ Done (Phase 1) | - |
| 19 | AUTONEGOTIATE_SINK | 0x37 | 24 | R/W | 2 | MEDIUM |
| 20 | POWER_STATUS | 0x3F | 2 | R | ✅ Done | - |
| 21 | PD_STATUS | 0x40 | 4 | R | ✅ Done | - |
| 22 | IO_CONFIG | 0x5C | 48 | R/W | 2 | MEDIUM |
| 23 | TYPEC_STATE | 0x69 | 4 | R | ✅ Done | - |
| 24 | SLEEP_CONTROL | 0x70 | 1 | R/W | 4 | LOW |
| 25 | GPIO_STATUS | 0x72 | 8 | R | ✅ Done (Phase 1) | - |
| 26 | LIQUID_DETECTION_CONFIG | 0x98 | 11 | R/W | 4 | LOW |

**Note:** For USB PD PDO type details, see [CONSTRAINTS.md - USB Power Delivery Complexity](../engineering/CONSTRAINTS.md#usb-power-delivery-complexity)

---

*End of Implementation Plan v0.2*
