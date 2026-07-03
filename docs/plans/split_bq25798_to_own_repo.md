# Plan: Split BQ25798 driver and the register-docs MCP server into their own repositories

## Context

The BQ25798 downstream-charger driver currently lives inside the TPS25751 library
(`include/BQ25798/`, `src/BQ25798/`) and now accounts for ~66% of the library's
source (~15,300 of ~23,000 LOC; 57 register classes). Exploration confirmed the
coupling is already plugin-shaped:

- BQ25798 code depends on exactly **two public TPS25751 headers**:
  `include/TPS25751Register.h` (decoder base class, 58 files) and
  `include/TPS25751DownstreamDevice.h` (device base, `BQ25798Device.h` only).
  Transitive deps (`TPS25751.h`, `TPS25751Task.h`, `TPS25751Debug.h`) are also
  public. No private `src/` headers, no `TPS25751BitUtils` usage.
- **Zero reverse dependencies**: TPS25751 core never includes or references
  BQ25798 code (one prose doc-comment mention in `TPS25751DownstreamDevice.h:14`).
  `include/TPS25751.h` (umbrella) is clean. BQ25798 has its **own factory
  singleton** (`BQ25798::Factory`), so the host factory needs no changes.

So the split is packaging + docs work, no code refactoring.

**Decisions made (with user):**
- **Three repos**, all siblings in `TPS25751-i2ct-test/lib/`:
  `lib/TPS25751` (host, existing), `lib/BQ25798` (charger driver, new),
  `lib/device-register-docs` (generic register-docs MCP server engine, new)
- Dependency mechanism: `lib_deps = symlink://…` for local dev (swap for git URL/registry when publishing)
- Git history: fresh start for both new repos (single import commit each; history stays in TPS25751 repo)
- MCP: the server repo holds `server.py` **and all device definitions**
  (`devices/tps25751/`, `devices/bq25798/`); the two library repos' `.mcp.json`
  files point at the sibling server repo. Key enabler verified in
  `server.py:63-67`: a relative `DEVICE_CONFIG` resolves against `server.py`'s
  own directory, so `devices/<name>/device.json` keeps working from any caller

## Step 1 — New repo skeletons

Create two sibling repos, each with `git init`, `.gitignore` (copy TPS25751's —
`.pio/` etc.), `LICENSE` (Apache-2.0, copy), `README.md`:
- `TPS25751-i2ct-test/lib/BQ25798/` — README adapted from TPS25751's BQ25798 sections
- `TPS25751-i2ct-test/lib/device-register-docs/` — README adapted from
  `docs/mcp-servers/device-register-docs/AGENTS.md`

## Step 2 — Move the code (paths preserved)

- `include/BQ25798/` → new repo `include/BQ25798/` — keeps
  `#include <BQ25798/BQ25798.h>` working unchanged for all consumers
- `src/BQ25798/` → new repo `src/BQ25798/`
- In TPS25751 repo: `git rm -r include/BQ25798 src/BQ25798`
- No source edits needed — all `#include` paths stay valid because the
  TPS25751 headers consumed are public and PlatformIO's LDF puts the TPS25751
  `include/` dir on the path via the library dependency

## Step 3 — Library manifests (new repo)

- `library.json`: name `BQ25798`, same author/license/schema as TPS25751's;
  `dependencies`: `Wire` + `TPS25751` (list with the git URL
  `https://github.com/forrestrae/TPS25751.git` for future publishing; local dev
  resolves via symlink `lib_deps`, not this entry)
- `library.properties`: `name=BQ25798`, `includes=BQ25798/BQ25798.h`,
  `depends=TPS25751`

## Step 4 — Examples and build environments

Move these 7 examples to the new repo's `examples/`:
`bq25798-status`, `bq25798-charge-config`, `bq25798-telemetry`,
`bq25798-set-charge`, `bq25798-fault-investigator`, `bq25798-direct-i2c`,
`read-downstream-bq25798-reg`.

- **New repo root `platformio.ini`**: mirror TPS25751's pattern —
  shared `[env]` (teensy40, 115200) plus per-example
  `[env:example-<name>]` with `build_src_filter = +<*> +<../examples/<name>/src>`,
  and add `lib_deps = symlink://../TPS25751` in `[env]` (relative symlink to the
  sibling checkout)
- **TPS25751 root `platformio.ini`**: delete the 7 `example-bq25798-*` /
  `example-read-downstream-bq25798-reg` envs (lines 15–34); keep
  `example-read-registers` and `example-load-patch-bundle`
- **Per-example standalone `platformio.ini`** files (bq25798-status,
  -charge-config, -telemetry, -fault-investigator): change
  `lib_deps = symlink://lib/TPS25751` to list both
  `symlink://lib/TPS25751` and `symlink://lib/BQ25798` (these assume the
  copy-into-your-project consumption mode)
- **`examples/README.md` + `examples/AGENTS.md`** in both repos: per the sync
  rule in `examples/AGENTS.md`, remove the moved rows from TPS25751's table and
  create the equivalent README/AGENTS pair in the new repo (copy the AGENTS.md
  conventions section; keep the 5 s I2Cr-pacing note)

## Step 5 — MCP server repo (`lib/device-register-docs`)

The server engine **and all device data** move to the new server repo:
- **Move** the entire `docs/mcp-servers/device-register-docs/` tree —
  `server.py`, `requirements.txt`, `AGENTS.md` (+ `CLAUDE.md` symlink), and
  both `devices/tps25751/` and `devices/bq25798/` folders — to the repo root
  (`lib/device-register-docs/server.py`, `.../devices/...`)
- **Move** the `.claude/skills/add-register-device` skill from TPS25751 to the
  server repo (it authors devices into `devices/`, which now lives here);
  update its internal paths from `docs/mcp-servers/device-register-docs/…` to
  repo-root-relative
- Server repo gets its own `.mcp.json` defining **both** servers (for working
  on the server/device data itself): command `uv run ./server.py`,
  `DEVICE_CONFIG=devices/<name>/device.json`; plus `.claude/settings.json`
  enabling them

Library repos point at the sibling server repo (relies on the side-by-side
`lib/` checkout layout, which is the chosen convention):
- **TPS25751** `.mcp.json`: keep only `tps25751-docs`, command becomes
  `uv run ../device-register-docs/server.py` with
  `DEVICE_CONFIG=devices/tps25751/device.json` (resolved against server.py's
  dir per `server.py:67`); mirror in `.cursor/mcp.json` and `.vscode/mcp.json`;
  drop the `bq25798-docs` permissions + `enabledMcpjsonServers` entry from
  `.claude/settings.json`; delete `docs/mcp-servers/` and the
  `add-register-device` skill
- **BQ25798** repo config files: `.mcp.json` with `bq25798-docs` only, command
  `uv run ../device-register-docs/server.py`,
  `DEVICE_CONFIG=devices/bq25798/device.json`; `.claude/settings.json`
  (`enabledMcpjsonServers: ["bq25798-docs"]` + the three
  `mcp__bq25798-docs__*` permissions); matching `.cursor/mcp.json` and
  `.vscode/mcp.json`

## Step 6 — Documentation split

Principle: the **generic downstream-device extension point stays in TPS25751**
(it's a host capability); **BQ25798-specific detail moves** to the new repo.

TPS25751 repo edits:
- `AGENTS.md` (edit AGENTS.md — `CLAUDE.md` is a symlink): remove the
  `### BQ25798 Downstream Device Driver` section, the `bq25798-docs` MCP table
  row, BQ25798 lines in the architecture/component list and directory tree;
  update the MCP section to say the server lives in the sibling
  `lib/device-register-docs` repo (invoked via `../device-register-docs/server.py`);
  add a short pointer: "BQ25798 driver now lives in the sibling `lib/BQ25798`
  repo, the reference implementation of the downstream-device tier"
- `docs/AGENTS.md` (`docs/CLAUDE.md` symlink) is mostly MCP-server
  documentation — trim it to a short pointer at the server repo (the detailed
  content moves with `device-register-docs/AGENTS.md`)
- `docs/engineering/ARCHITECTURE.md`: keep ADR-008/009/010 (they define the
  extension-point contract) but reword to reference BQ25798 as an *external*
  reference implementation; move the BQ25798-specific walkthrough sections
  (`### Downstream-Device Typed Write Flow (BQ25798, ADR-009)`, line ~771) to
  the new repo's docs
- `docs/engineering/CONSTRAINTS.md`: keep the generic I2Cr/I2Cw sections; move
  `## BQ25798 Downstream Device Decode & Encode Conventions` (line ~802) to the
  new repo
- Minor mention cleanup in `docs/engineering/STANDARDS.md`,
  `CODE_REVIEW_GUIDELINES.md`, `docs/AGENTS.md`, root `README.md`
- Historical plan/spec docs under `docs/plans/` and `docs/specs/` stay as-is
  (repo history)

New repo docs:
- `AGENTS.md` (+ `CLAUDE.md` symlink) — developer guide: the decode/encode
  conventions (16-bit big-endian assembly, `setField16BE`, signed ADC channels,
  LSB step sizes from SLUSE02 not MCP), factory pattern, dual transport, MCP
  usage, build commands
- `docs/engineering/` — the moved BQ25798-specific sections (decode/encode
  conventions, typed-write-flow walkthrough), referencing TPS25751's
  ARCHITECTURE.md for ADR-008/009/010

## Step 7 — Parent project check

`TPS25751-i2ct-test/` gains `lib/BQ25798` automatically (PlatformIO scans
`lib/`). Grep the parent project's `src/` for `BQ25798`; if used, no further
action (LDF resolves both libs); if not, nothing to do.

`lib/device-register-docs` is not a PlatformIO library but lives in `lib/`, so
the LDF will scan it. It contains no C/C++ sources and no `library.json`
(intentionally — per user, it doesn't need one), so the scan is a no-op.

## Execution strategy — delegate the mechanical work, keep judgment in the loop

Delegate the **mechanical implementation** (Steps 1–5, 7: file moves, manifests,
PlatformIO envs, MCP asset relocation, config edits) to a **Sonnet subagent**
working from this plan's step list. Instruct it to move files verbatim (no
"improvements" to moved code) and hand-author only the new files named in the
plan (manifests, `platformio.ini`, MCP configs).

**Fable handles the documentation surgery (Step 6) directly** — it is the one
non-mechanical slice: deciding which sentences of ADR-008/009/010 describe the
generic downstream-device extension point (stays in TPS25751) versus BQ25798
specifics (moves), and rewriting the AGENTS.md sections in both repos. That
boundary call requires judgment; if any of it is delegated anyway, Fable
closely reviews that slice line-by-line.

**Fable as quality gate** after the executor finishes:
1. Verify moves are byte-identical: `diff -r` the moved trees against
   `git show HEAD:` of the pre-split TPS25751 repo (or compare checksums).
2. Run the Verification section below — all example environments must compile
   in both library repos, plus the MCP smoke tests; for mechanical moves,
   compilation is the strongest correctness signal.
3. Run `/code-review` at **high** effort, scoped to the TPS25751 repo's
   removal/edit diff (platformio.ini, .mcp.json, settings, AGENTS/docs edits)
   and to the newly authored files in the BQ25798 repo — **not** the ~15k LOC
   of verbatim-moved register classes; the `diff -r` in (1) covers those
   exactly, and reviewing unchanged code is noise.

Fix findings (or send them back to the executor via SendMessage), then re-run
the affected verification step.

## Verification

1. **TPS25751 repo builds without BQ25798**: from the TPS25751 root,
   `pio run -e example-read-registers -e example-load-patch-bundle`
2. **New repo builds via the symlink dep**: from the BQ25798 root,
   `pio run` (all 7 example envs) — proves the plugin boundary holds with no
   source edits
3. **Clean separation**: `grep -ri bq25798 include/ src/` in TPS25751 returns
   only the prose comment in `TPS25751DownstreamDevice.h`
4. **MCP servers work cross-repo**: smoke-test the moved server directly
   (`cd lib/device-register-docs && DEVICE_CONFIG=devices/bq25798/device.json
   uv run server.py`, same for tps25751); then restart Claude Code in each
   library repo and confirm `tps25751-docs` loads in TPS25751 and
   `bq25798-docs` loads in BQ25798 via the `../device-register-docs/` path
   (e.g. run one `search_register` call each)
5. **Hardware (optional, user)**: flash `example-bq25798-status` to the Teensy
   and confirm charger reads over I2Cc still work
6. Commit: one removal commit in TPS25751, one import commit in each new repo
   (BQ25798, device-register-docs)
