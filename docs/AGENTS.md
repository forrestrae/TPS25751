# Register-Docs MCP Server (moved)

The device-generic register-docs MCP server that used to live in this repo
(`docs/mcp-servers/device-register-docs/`) — `server.py`, its `requirements.txt`,
and all device definitions (`devices/tps25751/`, `devices/bq25798/`) — now lives
in the sibling **`lib/device-register-mcp-server`** repository, alongside the
`/add-register-device` authoring skill.

This repo still *uses* the server: `.mcp.json` (and `.cursor/mcp.json` /
`.vscode/mcp.json`) define the `tps25751-docs` server as
`uv run ../device-register-mcp-server/server.py` with
`DEVICE_CONFIG=devices/tps25751/device.json` (a relative `DEVICE_CONFIG` resolves
against `server.py`'s own directory). This relies on the side-by-side `lib/`
checkout layout.

For usage instructions (tools, resources, troubleshooting, extending the server,
onboarding new devices), see `../device-register-mcp-server/AGENTS.md` in the server repo.
