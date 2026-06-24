---
name: add-register-device
description: >-
  Onboard a new hardware device to the device-generic register-docs MCP server
  (docs/mcp-servers/device-register-docs). Use when the user wants to "add a device",
  "serve another chip's registers", "convert a register table / datasheet into the
  MCP server's JSON", or "create a device.json manifest". Produces a device folder
  under devices/<name>/ (manifest + registers JSON + schema), validates it, and wires
  it up as a new MCP server. Authoring only — looking up existing registers is done
  with the per-device MCP tools (search_register / explain_bitfield / get_register).
---

# Add a register device to the docs MCP server

The MCP server at `docs/mcp-servers/device-register-docs/server.py` is device-generic:
one server binary serves whatever device a **`device.json` manifest** points it at
(via the `DEVICE_CONFIG` env var). Each device lives in its own folder:

```
docs/mcp-servers/device-register-docs/
  server.py
  devices/
    tps25751/   device.json + tps25751_registers_extracted.json + TPS25751-register-schema.json
    bq25798/    device.json + bq25798_registers_extracted.json  + BQ25798-register-schema.json
```

Each configured device is exposed as its own MCP server (tool prefix
`mcp__<server_name>__*`) by adding an entry to `.mcp.json`. Onboarding a new device
means producing its `devices/<name>/` folder and registering it.

This skill is **authoring only**. To *query* registers already served, use the MCP
tools (`search_register`, `explain_bitfield`, `get_register`) on the relevant server.

## Inputs you need from the user

1. **Device name** (e.g. `BQ25798`) — drives `device_name`, `class_prefix`, `uri_scheme`,
   and the `devices/<name>/` folder.
2. **A register source** — a datasheet/TRM register table as text, a PDF, or pasted rows.
   Each register needs: offset, name, width (bits), reset value, and its bit fields
   (bit range, name, access type, description, and any enumerated values).
3. **I2C address(es)** of the device (7-bit, e.g. `0x6B`). Ask if not given.

If the source is a large PDF, extract the register tables first (ask the user to paste
the relevant tables if extraction is unreliable). **Never invent register addresses,
bit positions, reset values, or enumerated meanings** — if a value is unknown, ask or
omit the field rather than guessing.

## The schema contract

Each device ships its **own** JSON Schema in its folder; the registers JSON must
validate against it. Start from an existing device's schema as the template — the two
in-repo schemas differ, so pick whichever fits the source data:

- **`devices/tps25751/TPS25751-register-schema.json`** — strict/minimal. Top level is
  `{ "registers": [...] }` only. `reset` must be hex-with-`h` (`^[0-9A-Fa-f]+h$`).
  Enumerated `values` keys are hex-with-`h` (plus the TPS-only `APP `/`BOOT`/`PTCH`).
- **`devices/bq25798/BQ25798-register-schema.json`** — richer. Allows top-level `device`
  / `source` / `register_count`; `reset` may be `"X"` (POR/strap-dependent); fields may
  carry `reset_sources` and an `encoding` object; `values` keys may be hex-with-`h`
  **or** binary-with-`b` (e.g. `"011b"`).

Read the chosen schema before writing — it is the contract. Conventions common to both:

- **Register** (required): `offset`, `name`, `width`, `reset`, `fields` (≥1).
  - `offset` is **hex-with-`h`** (e.g. `"48h"`, `"00h"`); no `0x` prefix.
  - `width` is an integer in bits.
- **Field** (required): `bit_field`, `name`, `access_type`, `reset` (BQ schema also
  requires `description`).
  - `bit_field` is `"high-low"` (e.g. `"5-3"`); a single bit is `"7-7"`, not `"7"`.
  - `access_type` is exactly `"R"`, `"R/W"`, or `"W"`.
- `additionalProperties` is **false** at every level — only emit keys the schema defines.

## Procedure

1. **Pick and read the schema** that matches your source data (see above), and skim an
   existing device folder (`devices/tps25751/` or `devices/bq25798/`) to mirror structure.
2. **Create `devices/<name>/`** and write:
   - `<name>_registers_extracted.json` (or `<name>_registers.json`) built from the
     source, one register at a time, following the schema. Keep descriptions faithful.
   - The matching **schema file** (copy/adapt the chosen template).
3. **Write the `device.json` manifest** in the same folder:
   ```json
   {
     "server_name": "<name-lower>-docs",
     "device_name": "<DEVICE>",
     "uri_scheme": "<name-lower>",
     "class_prefix": "<DevicePrefix>",
     "registers_file": "<name>_registers_extracted.json",
     "schema_file": "<the schema file you wrote>",
     "i2c_addresses": ["0x6B"],
     "i2c_protocol": "<one-line description of how this device is accessed over I2C>"
   }
   ```
   - The server resolves `registers_file` and `schema_file` **relative to the manifest's
     directory** (`devices/<name>/`). Since both live beside `device.json`, use plain
     filenames.
   - `uri_scheme` must be a clean token (lowercase, no spaces) — it becomes the resource
     URI prefix `<scheme>://register/...`.
   - `i2c_protocol` is optional; when omitted the `config/i2c` resource shows a neutral
     "see the datasheet" note instead of asserting a protocol.
4. **Validate** before declaring done:
   ```bash
   uv run --with jsonschema python3 - <<'PY'
   import json, jsonschema, pathlib
   base = pathlib.Path("docs/mcp-servers/device-register-docs/devices/<name>")
   schema = json.load(open(base / "<schema file>"))
   data   = json.load(open(base / "<registers file>"))
   jsonschema.validate(data, schema)
   print("OK:", len(data["registers"]), "registers valid")
   PY
   ```
   Fix every validation error — do not hand back an invalid file.
5. **Register it as an MCP server.** Add an entry to `.mcp.json` (and, if used,
   `.cursor/mcp.json` / `.vscode/mcp.json`), mirroring the existing devices:
   ```json
   "<name>-docs": {
     "type": "stdio",
     "command": "uv",
     "args": ["run", "./docs/mcp-servers/device-register-docs/server.py"],
     "env": { "DEVICE_CONFIG": "devices/<name>/device.json" }
   }
   ```
   Then add the three tool permissions to `.claude/settings.json`
   (`mcp__<name>-docs__search_register`, `…__explain_bitfield`, `…__get_register`) and
   list `<name>-docs` in `enabledMcpjsonServers`.
6. **Print how to run it standalone** for a quick check:
   ```bash
   DEVICE_CONFIG=devices/<name>/device.json \
     uv run docs/mcp-servers/device-register-docs/server.py
   ```
   (`DEVICE_CONFIG` is resolved relative to `server.py`'s directory.)

## Done criteria

- `devices/<name>/` contains a manifest, a registers JSON, and a schema; the registers
  JSON validates against the schema (step 4 prints OK).
- The device is registered in `.mcp.json` and permitted in `.claude/settings.json`.
- No invented data: every address/bit/reset/enum traces to the user's source.
