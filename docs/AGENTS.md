# TPS25751 Documentation MCP Server

This document describes the TPS25751 Documentation MCP (Model Context Protocol) server that provides intelligent access to TPS25751 technical documentation and development resources.

## Overview

The TPS25751 Documentation MCP server provides:
- Access to technical documentation and datasheets
- Register descriptions with bit field explanations
- I2C communication protocol details

The server is **device-generic**: a `device.json` manifest controls which chip's
registers are served. The default manifest targets the TPS25751, but the server
can serve any device by pointing `DEVICE_CONFIG` at a different manifest file.

## Server Configuration

Two MCP servers are configured locally for this project, both backed by the same
generic server binary pointed at different device definitions via `DEVICE_CONFIG`:

**Server Path**: `mcp-servers/device-register-docs/server.py`
**Servers**: `tps25751-docs` (→ `devices/tps25751/`), `bq25798-docs` (→ `devices/bq25798/`)
**Scope**: Local (project-specific)

## Available Resources

The server provides access to the following resources:

### 1. Register Documentation
All 27 TPS25751 registers are documented with bit field descriptions. Use the
`mcp__tps25751-docs__search_register` tool to find any register by name, address,
or keyword. For a register index, access the `tps25751://registers` resource.

### 2. I2C Configuration
- I2C addresses and communication protocol details
- Library constants and usage examples

**Access Pattern**: `@tps25751://config/i2c`

## Available Tools

The server provides three tools for development assistance:

### 1. `search_register`
Search for register information by name, address, or description.

**Usage**: "Search for registers related to power"
**Parameters**: 
- `query`: Register name, address, or search term

### 2. `explain_bitfield`
Get detailed explanations of specific bit fields in registers.

**Usage**: "Explain the ConnState field in the STATUS register"
**Parameters**:
- `register`: Register name (STATUS, MODE, etc.)
- `field`: Bit field name or position

### 3. `get_register`
Exact, case-insensitive lookup of a single register by name.

**Usage**: "Get the STATUS register"
**Parameters**:
- `name`: Register name (spaces or underscores, any case)

## Usage Examples

### Basic Resource Access
```
@tps25751://register/status
```
This will provide detailed information about the STATUS register including bit fields and usage examples.

```
@tps25751://registers
```
Lists all registers with their addresses and a one-line description.

### Search for Registers
Use the search tool to find registers by functionality:
```
"Search for registers related to power management"
"Find registers that contain connection state"
```

### Exact Register Lookup
```
"Get the STATUS register"
"Get the POWER_PATH_STATUS register"
```

### Explain Specific Bit Fields
Get detailed explanations of register bit fields:
```
"Explain the VbusStatus bit in the STATUS register"
"What does the PowerRole field indicate?"
```

## Integration with Development

### Library Integration
The server is used alongside the existing TPS25751 library classes:
- `TPS25751`: Main controller class
- `TPS25751Status`, `TPS25751Mode`, etc.: Register-specific classes
- `TPS25751BitUtils`: Bitfield manipulation utilities

### Build Integration
The server is aware of the PlatformIO build system and provides examples compatible with:
- Teensy 4.0 and 4.1 platforms
- Arduino framework
- 115200 baud serial communication

## Troubleshooting

### Server Connection Issues
If the MCP server is not available:
1. Verify Python 3.10+ is installed
2. Check that MCP requirements are installed: `pip install "mcp[cli]>=1.28.0" jsonschema`
3. Ensure the server script is executable: `chmod +x mcp-servers/device-register-docs/server.py`
4. Restart Claude Code

### Missing Registers
If a register can't be found:
1. Verify the device's registers JSON exists under
   `mcp-servers/device-register-docs/devices/<device>/`
2. Check the startup log for a schema-validation warning
3. Confirm you're querying the right server (`tps25751-docs` vs `bq25798-docs`)

### Resource Access
Resources are accessed using each device's `uri_scheme` — `tps25751://` for the
TPS25751 server and `bq25798://` for the BQ25798 server:
- Register index: `<scheme>://registers`
- Single register: `<scheme>://register/register_name`
- Configuration: `<scheme>://config/i2c`

## Development Notes

### Extending the Server
To add new resources or tools:
1. Edit `mcp-servers/device-register-docs/server.py` (changes apply to all devices)
2. Restart Claude Code to reload the servers

### Register Definitions
Register information is maintained per device in
`mcp-servers/device-register-docs/devices/<device>/<device>_registers_extracted.json`.
Update the JSON file (not `server.py`) when adding registers or modifying bit field
descriptions. The server loads it at startup and optionally validates it against that
device's schema in the same folder if `jsonschema` is installed. Use the
`/add-register-device` skill to onboard a new device.

## Additional Resources

- **TPS25751 Library**: `lib/TPS25751/`
- **Example Usage**: `src/main.cpp`
- **PlatformIO Configuration**: `platformio.ini`
- **Project Documentation**: `AGENTS.md` (root level)