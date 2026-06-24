# Device Register Documentation MCP Server

This is a **device-generic** MCP server that provides register documentation for
hardware devices, extracted from their official manufacturer manuals. One server binary
(`server.py`) serves whichever device a `device.json` manifest selects.

## Configured devices

Two devices are defined and exposed as two MCP servers (both run this same `server.py`
with a different `DEVICE_CONFIG`):

| MCP server | Device | Registers | Tool prefix |
|---|---|---|---|
| `tps25751-docs` | TPS25751 USB-C PD controller | 27 | `mcp__tps25751-docs__*` |
| `bq25798-docs` | BQ25798 buck-boost charger | 57 | `mcp__bq25798-docs__*` |

**Use `tps25751-docs`** for the host PD controller's own registers, and **`bq25798-docs`**
for the downstream charger reached over the TPS25751's I2Cc bus. Both expose the same
three tools and the same resource URIs (under each device's own `uri_scheme`).

## Layout

```
device-register-docs/
  server.py              # generic server; device chosen via DEVICE_CONFIG
  requirements.txt
  devices/
    tps25751/  device.json + tps25751_registers_extracted.json + TPS25751-register-schema.json
    bq25798/   device.json + bq25798_registers_extracted.json  + BQ25798-register-schema.json
```

Each device's registers JSON is the single source of truth for that device, validated
at startup against the schema in the same folder (when `jsonschema` is installed).

## Available Tools

Each server exposes three tools (shown below for `tps25751-docs`; the `bq25798-docs`
server exposes the identical tools under the `mcp__bq25798-docs__` prefix).

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

### `mcp__tps25751-docs__get_register`

Exact, case-insensitive lookup of a register by name. Unlike `search_register`
(which does fuzzy/keyword matching), this returns the single named register or
an error with the full list of available register names.

**Parameters:**
- `name` (string): Exact register name (spaces or underscores, any case)

**Examples:**
```
get_register("STATUS")
get_register("Power Path Status")
get_register("INTERRUPT_EVENT_FOR_I2C1")
```

**Returns:**
- Address, width, reset value, description, and all bit fields for the named register
- If not found: list of all available register names

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

2. **Fetch the exact register** using `mcp__tps25751-docs__get_register` with
   `name = "REGISTER_NAME"` for a structured single-register view.

3. **Understand specific fields** using `mcp__tps25751-docs__explain_bitfield` with
   `register = "REGISTER_NAME"` and `field = "field_name"`. This shows bit positions,
   access types, and enumerated values.

4. Implement the register class using the information from steps 1-3.

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

## Available Resources

In addition to the tools, the server exposes MCP resources queryable by URI.

### `tps25751://registers`

Index of all registers: name, address, and first line of description. Useful for
a quick overview without running a search query.

### `tps25751://register/{reg_name}`

Full register documentation for a single register (same content as `get_register`
but accessed as a resource rather than a tool call).

Example: `tps25751://register/STATUS`

### `tps25751://config/i2c`

I2C address list, protocol description, and a generic usage code snippet for the
configured device.

## Server Implementation

The MCP server (`server.py`) dynamically loads register data at startup:

1. Reads the configured registers JSON file (from `device.json` / `DEVICE_CONFIG`)
2. Optionally validates the JSON against the schema using `jsonschema` (logs a
   WARNING on failure but does not crash; logs INFO if `jsonschema` is not installed)
3. Converts hex format ("3h" → "0x03")
4. Formats bit fields with ranges and access types
5. Builds searchable register dictionary
6. Serves data through MCP protocol

### Key Functions

- `load_device_config()`: Load manifest file and merge with defaults
- `load_registers_from_json()`: Load JSON file with optional schema validation
- `convert_hex_offset_to_address()`: Convert "3h" to "0x03" format
- `format_bit_field_description()`: Format fields with enumerated values
- `convert_registers_to_legacy_format()`: Transform JSON to server format

## Serving a Different Device

The server is fully device-generic. To serve registers for a different chip:

1. Create a `device.json` manifest (anywhere on disk) with these fields:

   | Field | Type | Description |
   |---|---|---|
   | `server_name` | string | MCP server name (e.g. `"bq25798-docs"`) |
   | `device_name` | string | Human-readable chip name (e.g. `"BQ25798"`) |
   | `uri_scheme` | string | URI prefix for resources (e.g. `"bq25798"`) |
   | `class_prefix` | string | C++ class prefix for code snippets (e.g. `"BQ25798"`) |
   | `registers_file` | string | Path to registers JSON, relative to the manifest |
   | `schema_file` | string | Path to JSON schema for validation, relative to the manifest |
   | `i2c_addresses` | array | List of I2C addresses as hex strings (e.g. `["0x6B"]`) |
   | `i2c_protocol` | string | Optional one-line I2C access note for the `config/i2c` resource; omit for a neutral "see datasheet" note |

   All fields are optional — missing keys fall back to the TPS25751 defaults.

   By convention each device gets its own folder `devices/<name>/` holding the
   manifest, registers JSON, and schema (all referenced by plain filename).

2. Create a schema-valid `<device>_registers.json` and its schema beside the manifest.

3. Point the server at your manifest via the `DEVICE_CONFIG` env var (resolved relative
   to `server.py`'s directory when not absolute):

   ```bash
   DEVICE_CONFIG=devices/<name>/device.json uv run server.py
   ```

4. Register it as an MCP server by adding an entry to `.mcp.json` (and `.claude/settings.json`
   permissions) — see the `tps25751-docs` / `bq25798-docs` entries as templates. The
   `/add-register-device` skill automates this whole flow.

### Example: BQ25798

The BQ25798 device lives at `devices/bq25798/` (manifest + 57-register JSON +
`BQ25798-register-schema.json`) and is served as the `bq25798-docs` MCP server:

```bash
# From the device-register-docs/ directory:
DEVICE_CONFIG=devices/bq25798/device.json uv run server.py
```

Each device uses its **own** schema (the TPS25751 and BQ25798 schemas differ — the
BQ25798 one additionally allows `"X"` resets, `reset_sources`, and `encoding` metadata).

## Maintenance

### Updating Register Data

To update register definitions:

1. Edit the `devices/<device>/<device>_registers_extracted.json` file
2. Each device has its own schema in `devices/<device>/` (validated at startup)
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
- Verifying register implementations against the TI manual
- Understanding bit field meanings for debugging

See the root `AGENTS.md` for instructions on using this MCP server within the main project context.