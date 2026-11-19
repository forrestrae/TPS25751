# Register-Docs MCP Server (moved)

The device-generic register-docs MCP server that used to live in this repo
(`docs/mcp-servers/device-register-docs/`) — `server.py`, its `requirements.txt`,
and all device definitions (`devices/tps25751/`, `devices/bq25798/`) — now lives
in the sibling **`lib/device-register-mcp-server`** repository, alongside the
`/add-register-device` authoring skill.

This repo still *uses* the server, twice: `.mcp.json` (and `.cursor/mcp.json` /
`.vscode/mcp.json`) define `tps25751-docs` as
`uv --directory ../device-register-mcp-server run server.py` with
`DEVICE_CONFIG=devices/tps25751/device.json`, and `usb-pd-docs` the same way with
`DEVICE_CONFIG=specifications/USB_PD/specification.json` (a relative `DEVICE_CONFIG`
resolves against `server.py`'s own directory). `usb-pd-docs` serves the USB PD R3.3
data objects that TPS25751 registers 48h, 77h, 78h, 7Ah, 7Bh, 7Dh, and 7Eh carry, and
`tps25751-docs` names the object and bits for each of those registers in a
Specification block (from `devices/tps25751/spec_refs.json`). This relies on the
side-by-side `lib/` checkout layout. See the MCP section of `AGENTS.md` at the repo
root.

For usage instructions (tools, resources, troubleshooting, extending the server,
onboarding new devices), see `../device-register-mcp-server/AGENTS.md` in the server repo.
