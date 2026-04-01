# TPS25751 Documentation MCP Server

This document describes the TPS25751 Documentation MCP (Model Context Protocol) server that provides intelligent access to TPS25751 technical documentation and development resources.

## Overview

The TPS25751 Documentation MCP server provides:
- Access to technical documentation and datasheets
- Register descriptions with bit field explanations
- Code examples and usage patterns
- I2C communication protocol details
- Library file descriptions and purposes

## Server Configuration

The MCP server has been configured locally for this project:

**Server Name**: `tps25751-docs`  
**Server Path**: `mcp-servers/tps25751-docs/server.py`  
**Scope**: Local (project-specific)

## Available Resources

The server provides access to the following resources:

### 1. Documentation Files
- **TPS25751 Technical Reference Manual.pdf**: Comprehensive technical documentation
- **TPS25751 USB Type-C and USB PD Controller.pdf**: Device-specific documentation

**Access Pattern**: `@tps25751://doc/filename.pdf`

### 2. Register Documentation
All 27 TPS25751 registers are documented with bit field descriptions. Use the
`mcp__tps25751-docs__search_register` tool to find any register by name, address,
or keyword. For the complete list, use `mcp__tps25751-docs__list_library_files`.

### 3. I2C Configuration
- I2C addresses and communication protocol details
- Library constants and usage examples

**Access Pattern**: `@tps25751://config/i2c`

## Available Tools

The server provides four main tools for development assistance:

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

### 3. `generate_code_example`
Generate Arduino/PlatformIO code examples for common operations.

**Usage**: "Generate code to read STATUS and MODE registers"
**Parameters**:
- `operation`: Operation type (read_register, initialize, debug_print)
- `registers`: List of registers to include

### 4. `list_library_files`
List all TPS25751 library files with their purposes and descriptions.

**Usage**: "List all library files"
**Parameters**: None

## Usage Examples

### Basic Resource Access
```
@tps25751://register/status
```
This will provide detailed information about the STATUS register including bit fields and usage examples.

### Search for Registers
Use the search tool to find registers by functionality:
```
"Search for registers related to power management"
"Find registers that contain connection state"
```

### Generate Code Examples
Request code examples for specific operations:
```
"Generate code to initialize TPS25751 and read all status registers"
"Show me an example of reading the MODE register with error handling"
```

### Explain Specific Bit Fields
Get detailed explanations of register bit fields:
```
"Explain the VbusStatus bit in the STATUS register"
"What does the PowerRole field indicate?"
```

## Integration with Development

### Code References
The server provides code references in the format `file_path:line_number` when applicable, allowing easy navigation to source code locations.

### Library Integration
All examples use the existing TPS25751 library classes:
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
2. Check that MCP requirements are installed: `pip install mcp>=1.2.0 httpx>=0.25.0`
3. Ensure the server script is executable: `chmod +x mcp-servers/tps25751-docs/server.py`
4. Restart Claude Code

### Missing Documentation
If documentation files are not found:
1. Verify PDF files exist in `lib/TPS25751/docs/`
2. Check file permissions
3. Ensure relative paths are correct from the server script location

### Resource Access
Resources are accessed using the `tps25751://` URI scheme:
- Documentation: `tps25751://doc/filename.pdf`
- Registers: `tps25751://register/register_name`
- Configuration: `tps25751://config/i2c`

## Development Notes

### Extending the Server
To add new resources or tools:
1. Edit `mcp-servers/tps25751-docs/server.py`
2. Add new resources to `list_resources()`
3. Handle new URIs in `read_resource()`
4. Add new tools to `list_tools()` and `call_tool()`
5. Restart Claude Code to reload the server

### Register Definitions
Register information is maintained in `tps25751_registers_extracted.json`. Update
the JSON file (not `server.py`) when adding new registers or modifying bit field
descriptions. The server loads this file at startup.

### Code Generation Templates
Code examples are generated from templates in the `code_examples` dictionary. Customize these templates to match your coding style and project requirements.

## Additional Resources

- **TPS25751 Library**: `lib/TPS25751/`
- **Example Usage**: `src/main.cpp`
- **PlatformIO Configuration**: `platformio.ini`
- **Project Documentation**: `AGENTS.md` (root level)