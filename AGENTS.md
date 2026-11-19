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

**Last Updated:** 2026-11-18
**Current Phase:** Rev B register coverage, 35 of the 37 registers in TRM SLVUCR8B.
[trm-revB-audit-remaining-registers.md](docs/plans/trm-revB-audit-remaining-registers.md)
adds the ten registers rev B documents that the library did not decode, in Batches G
to K on `fix/rev-b-trm`. Batch G added VERSION (0x0F, read-only) and PD3_CONFIGURATION
(0x42, R/W), whose bits enable the controller's replies from the Tx data blocks that
later batches add. Batch H added GLOBAL_SYSTEM_CONFIGURATION (0x27, R/W, 14 bytes on
hardware against the TRM's 116 bits) and RECEIVED_SOP_IDENTITY (0x48, read-only, raw
VDOs). Batch I added TX_SOURCE_INFO (0x78, R/W, firmware-updated) and
TRANSMITTED_PPS_STATUS (0x7A, R/W), the source-side info blocks the controller sends in
reply to Get_Source_Info and Get_PPS_Status. Batch J added TX_SOURCE_CAPABILITIES_EXTENDED
(0x77, R/W) and TRANSMIT_SINK_CAPABILITIES_EXTENDED (0x7E, R/W), the data blocks the
controller sends in reply to Get_Source_Cap_Extended and Get_Sink_Cap_Extended, as raw
fields until Batch L adds typed decode. Earlier history: the 27 registers of plan-impl-v01 were complete on
2026-07-08 (Batch F added
LIQUID_DETECTION_CONFIG: a R/W 12-byte liquid/moisture-detection configuration block
— wait intervals (rev B "Wait Time"), ADC sample count/cadence, ADC threshold pairs with hysteresis, and
the state/enable bits for liquid detection + corrosion mitigation — implemented as a
straightforward `setBits()`-based R/W register mirroring `TPS25751AutoNegotiateSink`
(all bit offsets ≤ 82, well within `extractBits()`/`setBits()`'s `uint8_t bitPos`
range), with a cross-field low ≤ high semantic check on each threshold pair; Batch E
added AUTONEGOTIATE_SINK/IO_CONFIG: AUTONEGOTIATE_SINK is a genuine R/W sink-negotiation
config register (voltage window, min power, PPS request parameters) built on the
Batch B `setBits()` write primitive; IO_CONFIG is a read-only, boot-fixed 49-byte
GPIO configuration block that is the library's first register whose bit offsets
(up to 391) exceed `extractBits()`'s `uint8_t bitPos` range, so it decodes via direct
byte assembly (mirroring `TPS25751TransmitSourceCaps::getPDO()`) instead; Batch D
added TRANSMIT_SOURCE_CAPS/TRANSMIT_SINK_CAPS, the library's first **PDO encode**
path — R/W advertised source/sink capabilities with typed PDO word builders for all
four PDO types incl. PPS; Batch C added ADC_RESULTS, the internal-ADC telemetry
block — read-only voltage/current measurements; Batch B added INT_MASK1/INT_CLEAR1,
completing the interrupt-driven story alongside INT_EVENT1 and adding the base-class
`setBits()` write primitive + typed `writeRegister<T>()`; Batch A added
SLEEP_CONTROL/CUSTOMER_USE, the library's first host-register write path; COMMAND/DATA
enable the 4CC command-task interface and I2Cc downstream-device proxy); native
unit-test harness in place (`pio test -e native`, Batch 0)

---

## Device Register Documentation (MCP)

**IMPORTANT**: When working with TPS25751 registers, use the `tps25751-docs`
register-docs MCP server for accurate register information extracted from the TI
manuals. The generic server engine **and all device definitions** live in the
sibling **`lib/device-register-mcp-server`** repository; this repo's `.mcp.json` invokes
it cross-repo (`uv --directory ../device-register-mcp-server run server.py` with
`DEVICE_CONFIG=devices/tps25751/device.json` — a relative `DEVICE_CONFIG` resolves
against `server.py`'s own directory). This relies on the side-by-side `lib/`
checkout layout.

| MCP server | Tool prefix | Device | Use for |
|---|---|---|---|
| `tps25751-docs` | `mcp__tps25751-docs__*` | TPS25751 USB-C PD controller (37 registers) | The host PD controller's own registers (STATUS, MODE, COMMAND/DATA, …) |

(The BQ25798 charger's `bq25798-docs` server is configured in the sibling
`lib/BQ25798` driver repo, backed by the same server engine.)

**Available tools:** `search_register`, `explain_bitfield`, `get_register`.
The server provides register definitions with addresses, bit fields, enumerated value
meanings, and access types ([R], [R/W], [W]).

The server is **device-generic**: each device is defined under `devices/<device>/`
in the server repo by a `device.json` manifest plus its registers JSON and JSON
schema. Onboard a new device with the `/add-register-device` skill **in the server
repo**, then point an MCP server entry at it from the consuming repo's `.mcp.json`.

**Detailed documentation:** See `../device-register-mcp-server/AGENTS.md` (sibling repo) for complete usage instructions and examples.

### Configuring the MCP Servers Per Tool

The register-docs MCP server uses the standard MCP protocol and works with any compatible AI coding assistant. Tool-specific config files (each defining `tps25751-docs` against the sibling server repo) are included in this repo:

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

**Implemented Registers:** 35/37
- STATUS, MODE, POWER_PATH_STATUS, BOOT_FLAGS, PORT_CONFIG
- TYPEC_STATE, INTERRUPT_EVENT, POWER_STATUS, PD_STATUS
- ACTIVE_PDO_CONTRACT, ACTIVE_RDO_CONTRACT, PORT_CONTROL
- GPIO_STATUS, RECEIVED_SINK_CAPS, RECEIVED_SOURCE_CAPS
- COMMAND, DATA — the 4CC command-task control/payload registers (TRM Sec 4.4.2);
  see `TPS25751::executeCommand()` and `TPS25751DownstreamDevice`
- SLEEP_CONTROL, CUSTOMER_USE (Batch A, 2026-07-06) — the first R/W host registers
  with field setters and `writeSleepControl()`/`writeCustomerUse()` write paths
- INT_MASK1, INT_CLEAR1 (Batch B, 2026-07-07) — interrupt enable-mask and
  event-clear registers mirroring INT_EVENT1's 38-source bit map (36 in rev A; rev B adds bits 29 and 51); built on the new
  base-class `setBits()` write primitive and typed `TPS25751::writeRegister<T>()`
- ADC_RESULTS (Batch C, 2026-07-07) — read-only internal-ADC telemetry: per-pin
  voltage (14 mV/LSB; VBUS at 98 mV/LSB) and power-path current, mean and peak (16.5 mA/LSB, needs PORT_CONTROL.EnableCurrentMonitor), with raw
  + unit-converted accessors. Not updated by the controller in Unconnected-Sleep
  mode (a stale/all-zero read there is expected, not an error)
- TRANSMIT_SOURCE_CAPS, TRANSMIT_SINK_CAPS (Batch D, 2026-07-07) — R/W advertised
  capabilities; the library's first **PDO encode** path. Typed static PDO word
  builders for all four PDO types (Fixed/Battery/Variable/PPS, PPS at 100 mV/50 mA
  units) plus `setPDO`/`setPDOCount` and (source only) `setPowerPathForPDO`. Note
  the asymmetric layout: TRANSMIT_SOURCE (63 B) inserts two Power-Path bytes so
  PDO 1 starts at **byte 3**, while TRANSMIT_SINK (53 B) mirrors the Received-caps
  layout with PDO 1 at byte 1
- AUTONEGOTIATE_SINK, IO_CONFIG (Batch E, 2026-07-07) — AUTONEGOTIATE_SINK (0x37,
  24 B) is R/W sink-negotiation configuration (voltage window, minimum required
  power, capability-mismatch handling, PPS sink request parameters) with
  `setBits()`-based field setters and a cross-field semantic check (host-provided
  min voltage ≤ host-provided max voltage, skipped when either bound is
  PD-computed); IO_CONFIG (0x5C, 49 B) is **read-only** — the TRM states GPIO
  configuration is fixed at boot — and its bit offsets run up to 391, past
  `extractBits()`'s `uint8_t bitPos` range, so it decodes every field by direct
  byte assembly from `_raw[]` (mirroring `TPS25751TransmitSourceCaps::getPDO()`)
  rather than calling `extractBits()`
- LIQUID_DETECTION_CONFIG (Batch F, 2026-07-08) — R/W 12-byte (0x98)
  liquid/moisture-detection configuration: with/without-liquid wait intervals (rev B "Wait Time"; rev A "Sleep Time")
  (1000 ms/LSB), ADC sample count and sample-time cadence (10 ms/LSB), the two ADC
  threshold pairs (14 mV/LSB, each providing hysteresis for exit out of Liquid
  Detected), and the liquid-detection-state / enable-corrosion-mitigation /
  enable-liquid-detection bits. A straightforward `setBits()`-based R/W register
  mirroring `TPS25751AutoNegotiateSink` (all bit offsets ≤ 82, within
  `extractBits()`/`setBits()`'s `uint8_t bitPos` range) with a cross-field
  low ≤ high semantic check on each threshold pair. Completed the 27 registers of plan-impl-v01.
- VERSION, PD3_CONFIGURATION (Batch G, 2026-11-18): VERSION (0x0F, 4 B) is the
  read-only bootloader/application version, decoded as VVVV.MM.RR from a
  little-endian word. PD3_CONFIGURATION (0x42, 4 B, reset 00080010h) is R/W, with
  one `setBits()` setter per PD 3.x feature bit and `writePD3Config()`. Six of its
  bits enable the controller's replies to Get_* messages from 0x77, 0x78, 0x7A,
  0x7B, 0x7D, and 0x7E; with a bit clear the controller answers Not_Supported.
- GLOBAL_SYSTEM_CONFIGURATION, RECEIVED_SOP_IDENTITY (Batch H, 2026-11-18):
  GLOBAL_SYSTEM_CONFIGURATION (0x27) is R/W with typed-enum setters and
  `writeGlobalSystemConfig()`. Every write disconnects and reconnects the port, and
  the firmware keeps undocumented values in TRM-reserved bits, so writes must start
  from a fresh read. RECEIVED_SOP_IDENTITY (0x48, 25 B) is the latest Discover
  Identity response: Response Type, VDO count, and six raw VDOs. Typed VDO decode
  waits for Batch L.
- TX_SOURCE_INFO, TRANSMITTED_PPS_STATUS (Batch I, 2026-11-18): TX_SOURCE_INFO
  (0x78, 8 B) is R/W with raw and milliwatt accessors and clamping unit setters for
  its five PDP fields (0.5 W/LSB in the upper word, 1 W/LSB in the lower), and
  `writeTransmitSourceInfo()`. The firmware updates it automatically and may
  overwrite host writes. TRANSMITTED_PPS_STATUS (0x7A, 4 B) is R/W with output
  voltage (20 mV/LSB) and current (50 mA/LSB), their "not supported" sentinels, the
  temperature flag, the operating mode, and `writeTransmitPPSStatus()`. PD3_CONFIGURATION
  bits 18 and 20 gate the controller's replies from them.
- TX_SOURCE_CAPABILITIES_EXTENDED, TRANSMIT_SINK_CAPABILITIES_EXTENDED (Batch J,
  2026-11-18): 0x77 (15 B) and 0x7E (14 B) are R/W blocks of raw 8- and 16-bit
  fields, with setters, `writeTransmitSourceCapsExtended()`, and
  `writeTransmitSinkCapsExtended()`. The TRM defers every field encoding to the USB PD
  specification, so typed decode waits for Batch L. The controller prepends VID, PID,
  XID, and the firmware and hardware versions to these bytes. PD3_CONFIGURATION bits 8
  and 17 gate the controller's replies from them.

**Status:** 35 of 37 registers implemented. Batch K adds 7Bh and 7Dh.
- See [trm-revB-audit-remaining-registers.md](docs/plans/trm-revB-audit-remaining-registers.md) for the current roadmap
  and [plan-impl-v01.md](docs/plans/plan-impl-v01.md) for the completed rev A roadmap

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
  command-tasks; composes a `TPS25751` host rather than subclassing it. An alternate
  `(TwoWire&, addr)` constructor selects a **direct** (non-proxied) transport — plain
  Arduino-`Wire` register transactions for a part on the MCU's own bus, with none of
  the proxy constraints (no TRM 5 s spacing, no 63/11-byte caps); see ADR-010
- **`TPS25751PatchLoader`**: Sequences the 17-step Patch Burst Mode (PBM) flow from
  TI app note SLVAFV8A to load a firmware/configuration image directly into the
  TPS25751 when no EEPROM is present (PTCH → APP bring-up path). **Not a register
  class** — does not inherit `TPS25751Register` and is not wired into the factory.
  Builds entirely on `TPS25751::executeCommand()` (for the PBMs/PBMc 4CC handshakes)
  and `TPS25751::burstWrite()` (raw chunked burst to I2C address 0x30); see ADR-011
  in [ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md)
> **BQ25798 driver:** the typed driver for the BQ25798 buck-boost charger
> (`BQ25798::Device`) now lives in the sibling **`lib/BQ25798`** repository — it is
> the reference implementation of the downstream-device driver tier defined by
> ADR-008/009/010 in [ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md). It
> depends on this library (via `TPS25751Register` and `TPS25751DownstreamDevice`);
> nothing here depends on it.

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
- `TPS25751SleepControl`: SLEEP_CONTROL register (0x70) — sleep-mode enable + sleep-time; R/W with field setters
- `TPS25751CustomerUse`: CUSTOMER_USE register (0x06) — opaque 8-byte application blob; R/W raw/64-bit accessors
- `TPS25751InterruptMask`: INT_MASK1 register (0x16) — per-source interrupt enable mask mirroring INT_EVENT1's 38-source bit map (rev B adds MIDB Received bit 29 and Discover Mode Completed bit 51); R/W with `set<Source>(bool)` setters (a 1 unmasks/enables that source)
- `TPS25751InterruptClear`: INT_CLEAR1 register (0x18) — event-clear staging buffer (R/W per the TRM, p.23; the library only writes it); `clear<Source>()` / `clear(bitPos)` / `clearAll()` helpers (write a 1 to acknowledge that INT_EVENT1 source)
- `TPS25751ADCResults`: ADC_RESULTS register (0x6A) — read-only 13-byte internal-ADC telemetry block; per-pin voltage and power-path current (IVBUS_Mean byte 5, IVBUS_Peak byte 11 per rev B) with `raw*()` + unit-converted (`*Millivolts()`/`*Milliamps()`) accessors. Byte layout is not alphabetical (transcribed from the TRM); not updated in Unconnected-Sleep mode
- `TPS25751TransmitSourceCaps`: TRANSMIT_SOURCE_CAPABILITIES register (0x32) — R/W 63-byte advertised source capabilities; up to 7 PDOs starting at **byte 3** (bits 9:8 hold the Power Path for PDO 1; bits 23:10 are reserved in rev B, SLVUCR8B p.36). Mirrors `TPS25751ReceivedSourceCaps` decode + adds the encode path: static `build{FixedSupply,Battery,Variable,PPS}PDO()` word builders, `setPDO`/`setPDOCount`, and `get/setPowerPathForPDO`
- `TPS25751TransmitSinkCaps`: TRANSMIT_SINK_CAPABILITIES register (0x33) — R/W 53-byte advertised sink capabilities; up to 7 PDOs starting at byte 1 (same layout as the Received caps). Mirrors `TPS25751ReceivedSinkCaps` decode (Sink Fixed PDO: bit 28 = Higher Capability, bits 24:23 = Fast-Role-Swap current, a **2-bit** field) + adds the encode path
- `TPS25751AutoNegotiateSink`: AUTONEGOTIATE_SINK register (0x37) — R/W 24-byte sink-negotiation configuration (voltage window, minimum required power, capability-mismatch handling, PPS sink request parameters); `setBits()`-based field setters mirroring `TPS25751InterruptMask`'s pattern, plus a cross-field semantic check (host-provided min voltage ≤ host-provided max voltage when neither bound is PD-computed)
- `TPS25751IOConfig`: IO_CONFIG register (0x5C) — **read-only** 49-byte GPIO configuration block (output/interrupt/pull-up/pull-down/open-drain/initial-value/event-polarity per-GPIO bitmaps, analog-input selection, per-GPIO event mapping); "cannot be modified at run-time" per the TRM, so there are no setters, no write path, and no `kAddress`. Bit offsets run up to 391 (past `extractBits()`'s `uint8_t bitPos` range), so every field is decoded via direct byte assembly from `_raw[]`, mirroring `TPS25751TransmitSourceCaps::getPDO()`
- `TPS25751LiquidDetectionConfig`: LIQUID_DETECTION_CONFIG register (0x98) — R/W 12-byte (83 bits in the TRM; the controller's length byte is 12) liquid/moisture-detection configuration (with/without-liquid wait intervals (rev B "Wait Time"; rev A "Sleep Time") at 1000 ms/LSB, ADC sample count + sample-time cadence at 10 ms/LSB, two ADC threshold pairs at 14 mV/LSB providing hysteresis for exit out of Liquid Detected, and the liquid-detection-state / enable-corrosion-mitigation / enable-liquid-detection bits); `setBits()`-based field setters + `kAddress` write path mirroring `TPS25751AutoNegotiateSink` (all bit offsets ≤ 82, within `extractBits()`/`setBits()`'s `uint8_t bitPos` range — no direct byte assembly needed), plus a cross-field semantic check (low ≤ high on each threshold pair). The `numberOfSamples` "must be 2^N" TRM note is intentionally not enforced (reset value 0 is not a power of two)
- `TPS25751Version`: VERSION register (0x0F), read-only, 4 bytes. `rawVersion()`, `versionMajor()` (VVVV, bits 31:16), `versionMinor()` (MM, bits 15:8), and `versionRevision()` (RR, bits 7:0). The TRM gives only "byte 1 = RR, byte 2 = MM, etc.", so VVVV in bytes 3 and 4 is an inference. It is still unconfirmed: hardware read 0xF8091166, which gives an implausible VVVV of F809, so the decode stays as is until it can be compared with TI's tooling; `debugPrint()` shows hex `VVVV.MM.RR` because the TRM does not say whether the digits are BCD. No semantic rules; the all-zero reset is valid
- `TPS25751PD3Config`: PD3_CONFIGURATION register (0x42), R/W, 4 bytes, reset 00080010h (Support Get Revision and Unchunked Supported set). Bool accessors and `setBits()` setters for bits 20, 19, 18, 17, 11, 10, 8, 7, and 4, plus `kAddress` and `writePD3Config()`. Bits 20, 18, 17, 11, 10, and 8 gate the replies from 0x7A, 0x78, 0x7E, 0x7B, 0x7D, and 0x77. Bit 7 (Auto Get MIDB) stores its result in TX_MIBDB_SOP (0x73), which the library does not decode. Semantic validation requires reserved bits 31:21, 16:12, 9, and 6:5 to read 0. Bits 3:0 are reserved in the TRM but not checked, because the firmware sets them: hardware read 0x0008001A (see CONSTRAINTS.md, Read/Write Registers)
- `TPS25751GlobalSystemConfig`: GLOBAL_SYSTEM_CONFIGURATION register (0x27), R/W, 14 bytes. The TRM gives 116 bits (15 bytes), but the controller's length byte is 14 on hardware, so `RegisterInfo` uses 14. Typed enums with `toString()` and `setBits()` setters for RCP Threshold (23:22), PP3 Config (18:16), ILIM Over Shoot for PP_5V (15:14, not 0x28's APDO ILIM), PP1 Config (10:8), and PP Cable1 Switch (bit 0); `rawPp3Config()`/`rawPp1Config()` expose undefined codes. `kAddress` and `writeGlobalSystemConfig()`; a write disconnects and reconnects the port (TRM p.28). Semantic validation checks only PP3 ≤ 5 and PP1 ≤ 1. There are no reserved-bit checks, because the firmware sets bits in all four reserved ranges (hardware read `05 09 14 00 1C 03 00…`), so a write must start from a fresh read; `debugPrint()` shows the raw bytes for that reason (see CONSTRAINTS.md, Read/Write Registers)
- `TPS25751ReceivedSOPIdentity`: RECEIVED_SOP_IDENTITY_DATA_OBJECT register (0x48), read-only, 25 bytes. `numberValidVdos()` (2:0), nested `ResponseType` enum `responseType()` (7:6), zero-based `vdo(0..5)` via `extractBits32(8 + 32 * i, 32)` (0 for index ≥ 6), and aliases `idHeaderVdo()`, `certStatVdo()`, `productVdo()`. VDO internals are USB PD specification structures and are not decoded (Batch L). Semantic validation: count ≤ 6, reserved bits 5:3 read 0. Response Type is the authoritative status; INT_EVENT1 bit 51 is a later discovery step, not a "0x48 updated" signal
- `TPS25751TransmitSourceInfo`: TX_SOURCE_INFO register (0x78), R/W, 8 bytes, reset 8000000080000000h. Nested `PortType` enum (Managed, Guaranteed) for the two Port Type bits, `portTypeUpper()` (bit 63) and `portTypeLower()` (bit 31); the TRM gives both the same text and does not explain the duplication. `dpsPort()` (62); raw and `_mW` accessors for Port Maximum PDP and Port Guaranteed PDP at 0.5 W/LSB (49:41, 40:32) and Port Maximum, Present, and Reported PDP at 1 W/LSB (23:16, 15:8, 7:0). Raw setters mask to the field width; unit setters round down and clamp to the field maximum. `kAddress` and `writeTransmitSourceInfo()`. The TRM says the PD firmware updates the register automatically, so a host write can be overwritten. Semantic validation: reserved bits 61:50 and 30:24 read 0; DPSPort set requires bit 63 Managed (TRM); Guaranteed PDP at most Maximum PDP when the maximum is nonzero (inferred from the field descriptions, not stated by the TRM). Gated by PD3_CONFIGURATION bit 18 (`supportGetSourceInfo()`)
- `TPS25751TransmitPPSStatus`: TRANSMITTED_PPS_STATUS_DATA_BLOCK register (0x7A), R/W, 4 bytes. Output Voltage (15:0, 20 mV/LSB, 0xFFFF not supported) and Output Current (23:16, 50 mA/LSB, 0xFF not supported) with raw, `*Supported()`, and unit accessors (unit accessors return 0 for the sentinel); nested `PresentTemperatureFlag` (26:25; 00b, the reset, is undefined in the TRM) and `OperatingMode` (27, CV or CL) enums. Unit setters round down and clamp one below the sentinel; `setOutputVoltageNotSupported()` and `setOutputCurrentNotSupported()` write the sentinels. `kAddress` and `writeTransmitPPSStatus()`. Semantic validation: reserved bits 31:28 and 24 read 0. Gated by PD3_CONFIGURATION bit 20 (`supportPpsStatus()`)
- `TPS25751TransmitSourceCapsExtended`: TX_SOURCE_CAPABILITIES_EXTENDED_DATA_BLOCK register (0x77), R/W, 15 bytes. Raw accessors and `setBits()` setters for Voltage Regulation, Hold Up Time, Compliance, Touch Current (bytes 0 to 3), `peakCurrent(1..3)` (16-bit little-endian at bits 47:32, 63:48, 79:64; 0 for any other index), Touch Temperature (87:80), Source Inputs (95:88), Number Fixed Batteries (99:96), Number Hot Swappable Batteries (103:100), and Source PDP (110:104, 7 bits). `kAddress` and `writeTransmitSourceCapsExtended()`. No units or meanings: the TRM defers the encodings to the USB PD specification, and typed decode waits for Batch L. Semantic validation: reserved bits 119:111 read 0, each battery count at most 4 (TRM "Max of 4"). The battery counts should agree with 0x7B and 0x7D, which one register cannot check. Source Inputs bit 0 can be changed by the Barrel_Jack_Event GPIO event. Hardware reads Source PDP 0x7F, not the documented reset of 0 (see CONSTRAINTS.md, Read/Write Registers). Gated by PD3_CONFIGURATION bit 8 (`supportSourceExtended()`; the TRM calls it SourceCapExtMsg on p.54)
- `TPS25751TransmitSinkCapsExtended`: TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK register (0x7E), R/W, 14 bytes. Raw accessors and `setBits()` setters for SKEDB Version, Load Step, Sink Load Characteristics (31:16, 16-bit little-endian), Compliance, Touch Temperature (47:40; TI spells it "Touch Temprature"), Battery Info, Sink Modes, and Sink Minimum, Operational, and Maximum PDP (bytes 8 to 10). `kAddress` and `writeTransmitSinkCapsExtended()`. Raw only; typed decode waits for Batch L. Semantic validation: reserved bits 111:88 read 0; no PDP ordering rule, because the TRM gives no encoding for the PDP bytes. The TRM says the controller does not use these values to affect its behavior. Gated by PD3_CONFIGURATION bit 17 (`supportSinkCapExtended()`)

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

### Patch Loading (PBM) — EEPROM-less PTCH → APP Bring-up

A second consumer of `executeCommand()` is `TPS25751PatchLoader`
(`include/TPS25751PatchLoader.h`, `src/TPS25751PatchLoader.cpp`), which sequences
the Patch Burst Mode (PBM) flow from TI application note **SLVAFV8A** ("Using an
Embedded Controller (EC) to Load a Patch Bundle Directly to the TPS25751 or
TPS26750").

When the TPS25751 boots without an EEPROM it powers up in **PTCH** mode and waits
for the host to push the firmware+configuration image over I2Ct.  The 17-step flow:

1. (Optional) Wait for `I2Ct_IRQ` low.
2. Read MODE — confirm PTCH.
3. (IRQ mode) Configure `INT_MASK1` / clear `INT_CLEAR1`.
4-9. `executeCommand(PBMs, config[6])` — `config` = `bundleSize[4, LE] + 0x30 + 0x31`.
10. `burstWrite(0x30, image, length)` — raw chunked burst (no reg number, no length byte).
11. (IRQ mode) Clear `INT_CLEAR1`.
12-14. `executeCommand(PBMc)` — commit the bundle.
15. `delay(20)` ms.
16. Read `DATA[0]` — confirm `PatchStartStatus` == 0.
17. Read MODE — confirm APP.

Steps 4-9 and 12-14 are each a single `executeCommand()` call; only step 10 uses the
new `TPS25751::burstWrite()` primitive (the only addition to the core class).
`PatchLoadStatus` provides one distinct enumerator per failure point.

See ADR-011 in [ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md) and the PBM
subsection of [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) for protocol details
and correctness traps.

### Downstream Device Drivers (external)

Typed drivers for parts on the TPS25751's I2Cc bus are **separate libraries** built
on `TPS25751DownstreamDevice` — each keeps its own namespace, `Address` enum, and
per-device factory, and reuses `TPS25751Register` as its decoder base (the
extension-point contract is ADR-008/009/010 in
[docs/engineering/ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md); the recipe is
in its "Adding a Downstream Device Driver" section). The reference implementation
is the **BQ25798 charger driver in the sibling `lib/BQ25798` repository**; see that
repo's `AGENTS.md` for its API, decode/encode conventions, and examples.

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
├── .githooks/                          # Git workflow hooks (see "Git Workflow")
├── .cursor/mcp.json                    # Cursor MCP server config
├── .vscode/mcp.json                    # VS Code Copilot MCP server config
├── library.json                        # PlatformIO library manifest
├── library.properties                  # Arduino library manifest
├── scripts/                            # Release + git workflow tooling (verify.py, integrate.py, ...)
├── include/                            # Public header files
├── src/                                # Implementation files
├── examples/                           # Example sketches
└── docs/
    ├── AGENTS.md                       # Pointer to the register-docs MCP server repo
    ├── engineering/                    # Engineering standards (REQUIRED READING)
    │   ├── STANDARDS.md                # Implementation standards
    │   ├── CONSTRAINTS.md              # Platform constraints
    │   ├── ARCHITECTURE.md             # System design
    │   ├── TESTING.md                  # Testing requirements
    │   ├── DOCUMENTATION.md            # Documentation standards
    │   └── CODE_REVIEW_GUIDELINES.md   # Review checklist (embedded C++ / TPS25751-specific)
    └── plans/
        ├── plan-impl-v01.md            # Completed plan for the first 27 registers
        └── trm-revB-audit-remaining-registers.md  # Current plan: the other ten rev B registers
```

Sibling repositories (side-by-side in the parent project's `lib/`):

```
../BQ25798/                 # BQ25798 charger driver (depends on this library)
../device-register-mcp-server/    # Generic register-docs MCP server + all device definitions
```

**Navigation:**
- **Getting Started:** Read this file first
- **Understanding the System:** See [ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md)
- **Implementing Registers:** Start with [STANDARDS.md](docs/engineering/STANDARDS.md)
- **Platform Issues:** See [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md)
- **Writing Tests:** See [TESTING.md](docs/engineering/TESTING.md)
- **Reviewing Code:** See [CODE_REVIEW_GUIDELINES.md](docs/engineering/CODE_REVIEW_GUIDELINES.md)
- **Current Roadmap:** See [trm-revB-audit-remaining-registers.md](docs/plans/trm-revB-audit-remaining-registers.md)
- **MCP Servers:** See `../device-register-mcp-server/AGENTS.md` (sibling repo)
- **BQ25798 Driver:** See `../BQ25798/AGENTS.md` (sibling repo)

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
4. **Use the MCP server:**
   - `tps25751-docs` (`search_register`, `explain_bitfield`, `get_register`) for
     TPS25751 registers (served from the sibling `lib/device-register-mcp-server` repo)
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
3. Use the `tps25751-docs` MCP server for TPS25751 register details
4. Reference [Implementation Plan](docs/plans/plan-impl-v01.md) for detailed requirements
5. Check TI TPS25751 Technical Reference Manual for register specifications

---

## Git Workflow

Every change follows this workflow. A push to `main` publishes to the PlatformIO
Registry, so the local machine enforces the workflow with git hooks, and CI checks it
again before publishing. Design notes: [git-workflow-definition-enforcement.md](docs/plans/git-workflow-definition-enforcement.md).

### One-time setup per clone

```bash
scripts/setup_hooks.sh
```

This sets `core.hooksPath=.githooks`, makes plain `git merge` on `main` always create a
merge commit (`branch.main.mergeoptions=--no-ff`), makes `git pull` fast-forward only,
and records `main`'s current tip as the baseline that predates the workflow. Claude Code
and Codex sessions warn at start if the hooks are not active.

For Codex, also trust this repository (`codex --trust` or the trust prompt) and approve
its two hooks from `.codex/hooks.json` when Codex shows "Hooks need review". Codex runs
project hooks only in trusted projects and only after each hook's content is approved;
any later change to a hook needs approval again.

### Steps

1. Work on a topic branch, never on `main`. Name it `<type>/<kebab-topic>`, where type
   is one of `feat fix docs refactor test chore perf build ci` (for example
   `fix/rev-b-trm`).
2. Write commit subjects as Conventional Commits, `type(scope)!: subject`, with the same
   type list, at most 100 characters, imperative mood, and no attribution trailers. The
   only other accepted subjects are `Merge branch '<branch>'` and git's default
   `Revert "<subject>"`. One logical change per commit.
3. When the branch is done, rebase it onto `main`, commit or stash everything (the tree
   must be clean, untracked files included), and run `scripts/verify.py`. It lints the
   branch name and every commit, refuses merge commits on the branch and hand-edited
   version fields, then runs the script tests, `pio test -e native`, and
   `pio run -e example-read-registers`. A pass records the branch tip as verified.
4. Run `scripts/integrate.py <branch>` from the checkout that has `main`. It runs
   `git merge --no-ff <branch> -m "Merge branch '<branch>'"`, tags the tip
   `archive/<branch>`, and deletes the branch. Agents may run this step.
5. The human pushes: `git push origin main refs/tags/archive/<branch>`. Agents never push.
6. CI on `main` re-runs the gate, derives the release level, bumps, tags, and publishes
   (see "Publishing the Library"). Afterwards, `git pull` brings the bot's
   `chore(release)` commit into local `main`.

The `--no-ff` matters. A plain `git merge` of a branch that is already ahead of `main`
fast-forwards and silently ignores `-m`, which is how the `#minor` token for Batches A to
F never reached CI and the release shipped as v2.0.2.

### Enforcement

| Rule | Enforced by |
|---|---|
| Commit message format | `commit-msg` hook (`scripts/check_commits.py`); CI lints the release range |
| `main` moves only by a `--no-ff` merge whose second parent passed `verify.py` and whose tree equals that tip's tree | `reference-transaction` hook (`scripts/gitguard.py`). Direct commits, amends, resets, fast-forwards, and merges of stale or unverified branches on `main` are refused. Moves to commits already on `origin/main` (pulling a release) are allowed. |
| Agents never push | `pre-push` hook refuses every push from an agent shell, recognised by its environment: `CLAUDECODE` (Claude Code), `CODEX_THREAD_ID`, `CODEX_SESSION_ID`, `CODEX_TOOL_CALL_ID`, or `CODEX_SANDBOX` (Codex), or `WORKFLOW_AGENT=1` (any other agent that opts in). The agent guard also denies `git push`. |
| A human push of `main` is gated | `pre-push` re-checks each new first-parent commit with the same rule |
| Agents cannot switch the hooks off | `scripts/agent_guard.py`, a `PreToolUse` hook for Claude Code (`.claude/settings.json`) and Codex (`.codex/hooks.json`), denies `--no-verify`, `git commit -n`, changes to `core.hooksPath`, `branch.main.mergeoptions`, or `pull.ff`, `GIT_CONFIG_*` overrides, and writes to the workflow tooling or the verify stamp files, whether through a shell command, an edit tool, or a Codex `apply_patch` |
| Nothing failing is published | `publish.yml` job `verify` (lint, script tests, native tests, example build) gates job `publish` |
| Remote `main` is not rewritten or deleted | GitHub ruleset on `main`: block force push and deletion |

Workflow tooling is maintained by the human: `.githooks/`, `scripts/workflow_common.py`,
`gitguard.py`, `check_commits.py`, `release_level.py`, `verify.py`, `integrate.py`,
`setup_hooks.sh`, `agent_guard.py`, `.claude/settings*.json`, `.codex/`, and
`.github/workflows/publish.yml`. To let an agent change them, set `WORKFLOW_GUARD_OFF=1`
in the agent's environment for that session: for Claude Code, add
`"env": { "WORKFLOW_GUARD_OFF": "1" }` to `.claude/settings.local.json` (not the shared
`settings.json`) and remove it afterwards; for Codex, start it with the variable set.

Renaming or moving `agent_guard.py` breaks the hooks that call it, and Claude Code then
refuses every Bash and edit call until the path in `.claude/settings.json` matches again.
Update the hook configs first, then move the file.

If a refused fast-forward leaves files staged on `main`, restore the tree with
`git reset --merge HEAD`.

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

This directory is a **library** with a root `platformio.ini` that defines one
PlatformIO environment per bundled example (`[env:example-<name>]`).  Each
environment uses `build_src_filter` to pull the library sources together with the
example's `src/` directory, so examples build directly from this root.

```bash
# From this library root (where platformio.ini lives):
pio run -e example-read-registers            # compile the read-registers example
pio run -e example-load-patch-bundle         # compile the PBM patch-loader example
pio run -e example-read-registers -t upload  # build + flash to Teensy 4.0
pio device monitor                           # serial monitor @ 115200 baud

# List all available example environments:
pio project config
```

Replace `example-read-registers` with any `example-*` environment name — see
[`examples/README.md`](examples/README.md) for the full list and a description of
what each example demonstrates.

> **Older build path** (still valid when consuming as a published library):
> Copy the example out into your own PlatformIO project and add this library
> under `lib/`.

**Unit tests** run on the host — no hardware required:

```bash
pio test -e native                                    # all native unit tests
pio test -e native -f "test_native/test_TPS25751Status"  # one suite
```

The `[env:native]` environment compiles the platform-independent sources
against Arduino API stubs in `test/support/native/`; Unity suites live under
`test/test_native/` (one directory per register class —
`test_TPS25751Status` is the seed/reference suite). A green native test run is
a hard gate for every register batch. See
[TESTING.md](docs/engineering/TESTING.md) for the harness details, required
test cases, and how to add a suite. (Automated *coverage measurement* toward
the >90% target is still pending.)

### Publishing the Library

Publishing to the PlatformIO Registry is automated by
`.github/workflows/publish.yml` (design notes in
[docs/plans/platformio-pkg-publish.md](docs/plans/platformio-pkg-publish.md)):

- **Every push to `main`** first runs job `verify` (commit lint, script tests,
  `pio test -e native`, `pio run -e example-read-registers`). Only if it passes does
  job `publish` bump the version in `library.json` **and** `library.properties` (via
  `scripts/bump_version.py`, the single source of truth for bumps; never edit the
  version fields by hand), commit the bump back as
  `chore(release): vX.Y.Z [skip ci]`, push an annotated tag `vX.Y.Z`, and run
  `pio pkg publish --no-interactive`.
- The bump level is derived by `scripts/release_level.py` from every commit since the
  previous `chore(release)` commit: a breaking marker (`type!:` or a
  `BREAKING CHANGE:` footer) gives major, any `feat` gives minor, anything else gives
  patch. Merge commits are ignored. The old `#minor`/`#major` subject tokens no
  longer have any effect.
- The registry tarball is filtered by the `export.exclude` list in `library.json`
  (dev tooling, IDE configs, `docs/plans/`, `platformio.ini`, `scripts/`, `test/`
  are not shipped). Check what would be published with `pio pkg pack`.
- Auth: the workflow needs a `PLATFORMIO_AUTH_TOKEN` repository secret
  (generate with `pio account token`).

Future plans: additional platform support (ESP32, RP2040).

---
