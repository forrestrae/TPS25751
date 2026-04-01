# TPS25751 Documentation MCP Server

This MCP server provides comprehensive documentation for the TPS25751 USB-C Power Delivery controller, extracted from the official Texas Instruments Technical Reference Manual.

## Purpose

When working with TPS25751 registers, this MCP server provides:
- Accurate register addresses and descriptions
- Complete bit field definitions with access types
- Enumerated value meanings
- Code generation for Arduino/PlatformIO

## Data Source

The register definitions are loaded from `tps25751_registers_extracted.json`, which contains data extracted from the TPS25751 Technical Reference Manual and validated against the JSON schema in `TPS25751-register-schema.json`.

**The JSON file is the single source of truth for all register information.**

## Available Tools

### `mcp__tps25751-docs__search_register`

Search for registers by name, address, or keyword.

**Parameters:**
- `query` (string): Register name, address, or search term

**Examples:**
```
search_register("MODE")           # Find MODE register
search_register("0x1A")          # Find register at address 0x1A
search_register("interrupt")     # Find all interrupt-related registers
search_register("boot")          # Find boot-related registers
```

**Returns:**
- Register name and address
- Full description from TI manual
- All bit fields with positions, access types, and descriptions
- Enumerated values where applicable

### `mcp__tps25751-docs__explain_bitfield`

Get detailed information about specific bit fields within a register.

**Parameters:**
- `register` (string): Register name (e.g., "STATUS", "MODE")
- `field` (string): Bit field name or part of the name

**Examples:**
```
explain_bitfield("STATUS", "Connection State")
explain_bitfield("MODE", "Mode")
explain_bitfield("INTERRUPT_EVENT_FOR_I2C1", "Plug Insert")
```

**Returns:**
- Register address and description
- Matching bit field(s) with:
  - Bit positions (e.g., "bits 3:1" or "bit 30")
  - Access type: [R], [R/W], or [W]
  - Detailed description
  - Enumerated values (e.g., "0h = No connection", "1h = Port disabled")

### `mcp__tps25751-docs__generate_code_example`

Generate Arduino/PlatformIO code examples for TPS25751 operations.

**Parameters:**
- `operation` (string): "initialize", "read_register", or "debug_print"
- `registers` (array, optional): List of register names to include

**Examples:**
```
generate_code_example("initialize")
generate_code_example("read_register", ["Mode", "Status"])
generate_code_example("debug_print", ["Mode", "Status", "Power_Path_Status"])
```

**Returns:**
- Complete C++ code with correct includes
- Proper class instantiation
- Register reading and error handling
- Compatible with Teensy/Arduino platform

### `mcp__tps25751-docs__list_library_files`

List available TPS25751 library files and their purposes.

**Parameters:** None

**Returns:**
- Complete list of library header and source files
- Description of each file's purpose
- Library path information

## Usage Guidelines

### When to Use the MCP Server

**ALWAYS use the MCP server when:**
1. Looking up register addresses
2. Understanding bit field meanings
3. Writing code that reads/writes TPS25751 registers
4. Debugging register values
5. Adding new register support to the library

**DO NOT:**
- Guess register addresses or bit positions
- Use outdated documentation
- Make assumptions about enumerated values

### Typical Workflow

**Adding support for a new register:**

1. **Search for the register** using `mcp__tps25751-docs__search_register` with
   `query = "REGISTER_NAME"`. This gives you the address, description, and all fields.

2. **Understand specific fields** using `mcp__tps25751-docs__explain_bitfield` with
   `register = "REGISTER_NAME"` and `field = "field_name"`. This shows bit positions,
   access types, and enumerated values.

3. **Generate code template** using `mcp__tps25751-docs__generate_code_example` with
   `operation = "read_register"` and `registers = ["REGISTER_NAME"]`. This creates
   the boilerplate code.

4. Implement the register class using the information from steps 1-2.

## Register Coverage

The MCP server includes **27 registers** from the TPS25751:

- Mode (0x03)
- Customer Use (0x06)
- Command Register for I2C1 (0x08)
- Data Register for CMD1 (0x09)
- Interrupt Event for I2C1 (0x14)
- Interrupt Mask for I2C1 (0x16)
- Interrupt Clear for I2C1 (0x18)
- Status (0x1A)
- Power Path Status (0x26)
- Port Configuration (0x28)
- Boot Status (0x2C)
- Boot Flags (0x2D)
- PD Status (0x40)
- Type C Status (0x69)
- RX Source Capabilities (0x30)
- Active RDO (0x44)
- Active Contract PDO (0x34)
- Active Contract RDO (0x35)
- System Configuration (0x61)
- Sleep Configuration (0x62)
- Device Capabilities (0x0D)
- GPIO Status (0x68)
- GPIO Configuration (0x64)
- GPIO Configuration Port 2 (0x65)
- Temperature (0x66)
- Vendor Specific Register (0x67)
- ADC Results (0x6A)
- IO Config (0x5C)

## Data Format

### JSON Schema

Registers follow this structure:
```json
{
  "offset": "3h",           // Hex offset with 'h' suffix
  "name": "Mode",
  "width": 32,              // Width in bits
  "reset": "00000000h",     // Reset value
  "description": "...",
  "fields": [
    {
      "bit_field": "31-0",
      "name": "Mode",
      "access_type": "R",   // R, R/W, or W
      "reset": "0h",
      "description": "...",
      "values": {           // Optional enumerated values
        "APP ": "Application mode",
        "BOOT": "Boot mode"
      }
    }
  ]
}
```

## Server Implementation

The MCP server (`server.py`) dynamically loads register data at startup:

1. Reads `tps25751_registers_extracted.json`
2. Converts hex format ("3h" → "0x03")
3. Formats bit fields with ranges and access types
4. Builds searchable register dictionary
5. Serves data through MCP protocol

### Key Functions

- `load_registers_from_json()`: Load JSON file with error handling
- `convert_hex_offset_to_address()`: Convert "3h" to "0x03" format
- `format_bit_field_description()`: Format fields with enumerated values
- `convert_registers_to_legacy_format()`: Transform JSON to server format

## Maintenance

### Updating Register Data

To update register definitions:

1. Edit `tps25751_registers_extracted.json`
2. Validate against `TPS25751-register-schema.json`
3. Restart the MCP server (it loads JSON at startup)
4. No code changes needed in `server.py`

### Adding New Registers

Add new register entries to the JSON file following the schema:
- Use hex format with 'h' suffix (e.g., "3h")
- Include all required fields: offset, name, width, reset, fields
- Specify access_type for each field: "R", "R/W", or "W"
- Add enumerated values where applicable

## Integration with Main Project

The main TPS25751 Arduino/PlatformIO project uses this MCP server for:
- Looking up register information during development
- Generating code templates for new register classes
- Verifying register implementations against the TI manual
- Understanding bit field meanings for debugging

See the root `AGENTS.md` for instructions on using this MCP server within the main project context.