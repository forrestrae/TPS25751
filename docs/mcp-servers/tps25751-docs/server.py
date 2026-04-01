#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "mcp[cli]>=1.2.0",
# ]
# ///
"""
TPS25751 Documentation MCP Server

This server provides access to TPS25751 technical documentation,
including register descriptions, configuration options, and technical specifications.
"""

import json
import logging
import sys
from pathlib import Path
from typing import Any, Dict, List

from mcp.server.fastmcp import FastMCP

# Configure logging to stderr to avoid interfering with stdio transport
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    stream=sys.stderr
)
logger = logging.getLogger("tps25751-docs-server")

mcp = FastMCP("tps25751-docs-server")

# Base path to documentation
# Server is at: lib/TPS25751/docs/mcp-servers/tps25751-docs/server.py
# parent.parent.parent.parent gets us to lib/TPS25751/
DOC_BASE_PATH = Path(__file__).parent.parent.parent.parent

I2C_ADDRESSES = ["0x20", "0x21", "0x22", "0x23"]


# ---------------------------------------------------------------------------
# Data loading helpers
# ---------------------------------------------------------------------------

def load_registers_from_json() -> Dict[str, Any]:
    """Load register definitions from the extracted JSON file."""
    json_path = Path(__file__).parent / "tps25751_registers_extracted.json"
    try:
        with open(json_path) as f:
            return json.load(f)
    except FileNotFoundError:
        logger.error(f"Register JSON file not found: {json_path}")
        return {"registers": []}
    except json.JSONDecodeError as e:
        logger.error(f"Failed to parse register JSON: {e}")
        return {"registers": []}


def convert_hex_offset_to_address(offset: str) -> str:
    """Convert hex offset format (e.g., '3h') to standard address format (e.g., '0x03')."""
    return f"0x{int(offset.rstrip('h'), 16):02X}"


def format_bit_field_description(field: Dict[str, Any]) -> str:
    """Format a field definition into a human-readable string."""
    bit_field = field.get("bit_field", "")
    name = field.get("name", "")
    description = field.get("description", "")
    access_type = field.get("access_type", "")

    if "-" in bit_field:
        high, low = bit_field.split("-")
        bit_desc = f"bit {high}" if high == low else f"bits {high}:{low}"
    else:
        bit_desc = f"bit {bit_field}"

    result = f"{name} ({bit_desc})"
    if access_type:
        result += f" [{access_type}]"
    if description:
        result += f": {description}"
    if field.get("values"):
        result += "\n    Values:"
        for value, value_desc in field["values"].items():
            result += f"\n      {value}: {value_desc}"

    return result


def convert_registers_to_legacy_format(register_data: Dict[str, Any]) -> Dict[str, Any]:
    """Convert JSON register format to the internal format expected by the server."""
    legacy_registers = {}
    for register in register_data.get("registers", []):
        reg_name = register.get("name", "").upper().replace(" ", "_")
        address = convert_hex_offset_to_address(register.get("offset", "0h"))
        fields = [format_bit_field_description(f) for f in register.get("fields", [])]
        legacy_registers[reg_name] = {
            "address": address,
            "description": register.get("description", ""),
            "fields": fields,
            "width": register.get("width", 8),
            "reset": register.get("reset", "0h"),
        }
    return legacy_registers


# Load register data at startup
_REGISTER_DATA = load_registers_from_json()
TPS25751_REGISTERS = convert_registers_to_legacy_format(_REGISTER_DATA)


# ---------------------------------------------------------------------------
# Resources
# ---------------------------------------------------------------------------

@mcp.resource("tps25751://register/{reg_name}")
def register_resource(reg_name: str) -> str:
    """TPS25751 register documentation resource."""
    key = reg_name.upper()
    if key not in TPS25751_REGISTERS:
        return f"Unknown register: {reg_name}"

    reg_info = TPS25751_REGISTERS[key]
    class_name = f"TPS25751{key.title().replace('_', '')}"
    var_name = key.lower().replace("_", "")

    fields_text = "\n".join(f"  • {f}" for f in reg_info["fields"])
    return f"""TPS25751 {key} Register (Address: {reg_info['address']})

Description: {reg_info['description']}

Bit Fields:
{fields_text}

Usage in Code:
```cpp
{class_name} {var_name};
if ({var_name}.readFromChip(tps25751)) {{
    {var_name}.debugPrint();
}}
```

Library Class: {class_name}
Header File: {class_name}.h
"""


@mcp.resource("tps25751://config/i2c")
def i2c_config_resource() -> str:
    """TPS25751 I2C configuration and protocol details."""
    addrs = "\n".join(f"  • Address {i}: {addr}" for i, addr in enumerate(I2C_ADDRESSES))
    return f"""TPS25751 I2C Configuration

Default I2C Addresses:
{addrs}

I2C Protocol:
The TPS25751 uses a custom I2C protocol where responses are prefixed with a byte count.

Communication Flow:
1. Send register address to read
2. Receive byte count (first byte of response)
3. Receive actual register data (remaining bytes)

Example Usage:
```cpp
#include "TPS25751.h"

TPS25751 tps25751(Wire, TPS25751_I2CT_ADDRESS_IDX_0); // Use address 0x20

void setup() {{
    Wire.begin();
    tps25751.begin();
}}

void loop() {{
    TPS25751Status status;
    if (status.readFromChip(tps25751)) {{
        status.debugPrint();
    }}
    delay(1000);
}}
```

Library Constants:
- TPS25751_I2CT_ADDRESS_IDX_0: 0x20
- TPS25751_I2CT_ADDRESS_IDX_1: 0x21
- TPS25751_I2CT_ADDRESS_IDX_2: 0x22
- TPS25751_I2CT_ADDRESS_IDX_3: 0x23
"""


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

@mcp.tool()
def search_register(query: str) -> str:
    """Search for TPS25751 register information by name, address, or keyword.

    Args:
        query: Register name, address (e.g. 0x1A), or search term (e.g. "interrupt")
    """
    q = query.upper()
    results: List[str] = []

    for reg_name, reg_info in TPS25751_REGISTERS.items():
        if (q in reg_name
                or q in reg_info["address"].upper()
                or q.lower() in reg_info["description"].lower()):
            fields_text = "\n".join(f"  • {f}" for f in reg_info["fields"])
            results.append(
                f"**{reg_name} Register (Address: {reg_info['address']})**\n"
                f"{reg_info['description']}\n\n"
                f"Bit Fields:\n{fields_text}"
            )

    if not results:
        return f"No registers found matching query: {query}"

    return f"TPS25751 Register Search Results for '{query}':\n\n" + "\n\n---\n\n".join(results)


@mcp.tool()
def explain_bitfield(register: str, field: str) -> str:
    """Get detailed information about a specific bit field within a register.

    Args:
        register: Register name (e.g. STATUS, MODE, POWER_PATH_STATUS)
        field: Bit field name or part of the name (e.g. "Connection State", "Mode")
    """
    key = register.upper()
    if key not in TPS25751_REGISTERS:
        available = ", ".join(TPS25751_REGISTERS.keys())
        return f"Unknown register: {register}. Available registers: {available}"

    reg_info = TPS25751_REGISTERS[key]
    matching = [f for f in reg_info["fields"] if field.lower() in f.lower()]

    if not matching:
        all_fields = "\n".join(f"  • {f}" for f in reg_info["fields"])
        return (
            f"Field '{field}' not found in {key} register.\n"
            f"Available fields:\n{all_fields}"
        )

    matching_text = "\n".join(f"  • {f}" for f in matching)
    return (
        f"**{key} Register — {field}**\n\n"
        f"Register Address: {reg_info['address']}\n"
        f"Register Description: {reg_info['description']}\n\n"
        f"Matching Fields:\n{matching_text}"
    )


@mcp.tool()
def generate_code_example(operation: str, registers: List[str] | None = None) -> str:
    """Generate Arduino/PlatformIO code examples for TPS25751 operations.

    Args:
        operation: One of "initialize", "read_register", or "debug_print"
        registers: Optional list of register names to include (e.g. ["STATUS", "MODE"])
    """
    regs = registers or ["STATUS"]

    def class_name(r: str) -> str:
        return f"TPS25751{r.title().replace('_', '')}"

    def var_name(r: str) -> str:
        return r.lower().replace("_", "")

    if operation == "initialize":
        code = '''#include "TPS25751.h"

TPS25751 tps25751(Wire, TPS25751_I2CT_ADDRESS_IDX_0); // 0x20

void setup() {
    Serial.begin(115200);
    Wire.begin();
    tps25751.begin();
    Serial.println(F("TPS25751 initialized"));
}'''

    elif operation == "debug_print":
        includes = "\n".join(f'#include "{class_name(r)}.h"' for r in regs)
        reads = "\n".join(
            f'    {class_name(r)} {var_name(r)};\n'
            f'    if ({var_name(r)}.readFromChip(tps25751)) {{\n'
            f'        {var_name(r)}.debugPrint();\n'
            f'    }}'
            for r in regs
        )
        code = f'''#include "TPS25751.h"
{includes}

TPS25751 tps25751(Wire, TPS25751_I2CT_ADDRESS_IDX_0);

void loop() {{
{reads}

    delay(1000);
}}'''

    else:  # read_register (default)
        includes = "\n".join(f'#include "{class_name(r)}.h"' for r in regs)
        reads = "\n".join(
            f'    {class_name(r)} {var_name(r)};\n'
            f'    if ({var_name(r)}.readFromChip(tps25751)) {{\n'
            f'        Serial.println(F("Read {r} register OK"));\n'
            f'    }}'
            for r in regs
        )
        code = f'''#include "TPS25751.h"
{includes}

TPS25751 tps25751(Wire, TPS25751_I2CT_ADDRESS_IDX_0);

void loop() {{
{reads}

    delay(1000);
}}'''

    title = operation.replace("_", " ").title()
    return f"**TPS25751 {title} Example**\n\n```cpp\n{code}\n```"


@mcp.tool()
def list_library_files() -> str:
    """List available TPS25751 library files and their purposes."""
    lib_path = Path(__file__).parent.parent.parent.parent

    file_descriptions = {
        "TPS25751.h/cpp": "Main controller class for I2C communication",
        "TPS25751Status.h/cpp": "STATUS register (0x1A) — connection state, VBUS, power role",
        "TPS25751Mode.h/cpp": "MODE register (0x03) — operating mode and capabilities",
        "TPS25751PowerPathStatus.h/cpp": "POWER_PATH_STATUS register (0x26) — power path and charging",
        "TPS25751BootFlags.h/cpp": "BOOT_FLAGS register (0x2D) — boot and initialization flags",
        "TPS25751PortConfig.h/cpp": "PORT_CONFIG register (0x29) — port configuration settings",
        "TPS25751Register.h/cpp": "Base class for all register implementations",
        "TPS25751RegisterAddress.h": "Register address definitions and constants",
        "TPS25751Types.h": "Type definitions and enums",
        "TPS25751I2C.h": "I2C protocol-specific definitions",
        "util/TPS25751BitUtils.h/cpp": "Bitfield extraction and manipulation utilities",
    }

    lines = ["**TPS25751 Library Files**\n"]
    for name, desc in file_descriptions.items():
        lines.append(f"• **{name}**: {desc}")
    lines.append(f"\n**Library Location**: {lib_path}")
    lines.append("**Include Path**: lib/TPS25751/include/")
    lines.append("**Source Path**: lib/TPS25751/src/")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    mcp.run()
