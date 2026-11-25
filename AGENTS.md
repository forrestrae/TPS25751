# TPS25751 library: developer guide

An object-oriented C++ library for the Texas Instruments TPS25751 USB-C Power Delivery
controller over I2C, for the Arduino framework, published to the PlatformIO Registry. It
is not tied to a board or an MCU: the manifests declare every platform, and the Teensy
4.0 in `platformio.ini` is only the development build target. Keep board-specific
material in `examples/` and `docs/plans/`. `CLAUDE.md` is a symlink to this file; edit `AGENTS.md`.

The headers in `include/` are the API reference. This guide holds what applies to every
change, and points to the place where the detail for each area lives.

## Current state

- All 37 registers of TRM SLVUCR8B rev B have a class, with typed USB PD decode from USB
  PD R3.3 for the registers that carry specification objects.
- The 4CC command-task interface, the I2Cc downstream-device proxy, the Patch Burst Mode
  loader, and the EEPROM updater are implemented and were run on hardware.
- A bench console (`TPS25751BenchConsole`) and its MCP server (`tools/pd-bench`) give a
  host program register reads, allowlisted tasks and writes, and restarts over a serial
  line protocol, for checks against a USB PD analyzer.
- Native tests cover 99% of the lines in `src/` (`test/coverage.sh`, 2026-11-24).
- Plans and their hardware results are in `docs/plans/`. They are history: do not treat a
  plan as a description of the current code.

## Where the detail lives

Read the file for the area before changing it. Claude Code loads a sub-directory
`AGENTS.md` (through its `CLAUDE.md` symlink) when it reads a file in that directory;
other agents load only this file, so open the sub-directory file yourself.

| Working on | Read first |
|---|---|
| A register class in `include/` or `src/` | [include/AGENTS.md](include/AGENTS.md), then [STANDARDS.md](docs/engineering/STANDARDS.md) |
| Tests in `test/` | [test/AGENTS.md](test/AGENTS.md), then [TESTING.md](docs/engineering/TESTING.md) |
| Example sketches | [examples/AGENTS.md](examples/AGENTS.md) |
| `scripts/`, hooks, releases | [scripts/AGENTS.md](scripts/AGENTS.md) |
| The bench console, its protocol, or the bench server | `include/TPS25751BenchProtocol.h` and `include/TPS25751BenchConsole.h`, then [tools/pd-bench/README.md](tools/pd-bench/README.md) |
| Checking register decode against a USB PD capture | The `tps25751-pd-verify` skill, [plugins/tps25751-bench/skills/tps25751-pd-verify/SKILL.md](plugins/tps25751-bench/skills/tps25751-pd-verify/SKILL.md). Claude Code gets it from the plugin, Codex through the `.agents/skills` symlink. Both servers it drives are configured below |
| The bus protocol, 4CC tasks, PBM, flash tasks, or a difference between hardware and the TRM | [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md) |
| Structure, data flow, and the decision records (ADRs) | [ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md) |
| Reviewing a change | [CODE_REVIEW_GUIDELINES.md](docs/engineering/CODE_REVIEW_GUIDELINES.md) |

## Register documentation (MCP)

Take register and field definitions from the MCP servers, not from memory. Both run from
the sibling `lib/device-register-mcp-server` repository through this repository's
`.mcp.json`, which relies on the side-by-side `lib/` checkout. Usage, data files, and how
to add a device are in `../device-register-mcp-server/AGENTS.md`.

| MCP server | Source | Use for |
|---|---|---|
| `tps25751-docs` | TI TRM SLVUCR8B rev B, 37 registers | The controller's own registers: addresses, bit fields, values, access |
| `usb-pd-docs` | USB PD R3.3 V1.0, Tables 6.6 to 6.73 | The data objects that registers 30h to 35h, 48h, 77h, 78h, 7Ah, 7Bh, 7Dh, and 7Eh carry |

Both provide `search_register`, `explain_bitfield`, `get_register`, and `list_registers`.
For those thirteen registers `tps25751-docs` prints a Specification block that names the
USB PD table and bits each field maps to; look the object up in `usb-pd-docs`
(`get_register` takes a table number such as `6.59`) and cite both the TRM page and the
specification table.

Two more servers talk to hardware. `.mcp.json` (Claude Code) and `.codex/config.toml`
(Codex, trusted project) configure all four, and each agent's own file is the
authority on what is allowed without a prompt.

| MCP server | Source | Use for |
|---|---|---|
| `km003c` | Fork `forrestrae/mcp-powerz-km003c`, pinned tag, run with `uvx` | The KM003C meter: measurements and USB PD captures, written under `captures/`. Trigger tools are off unless `KM003C_ENABLE_TRIGGER=1` |
| `tps25751-bench` | `tools/pd-bench`, pinned to a release tag of this library, run with `uvx` | The board through the bench console. `bench_task`, `bench_write`, and `bench_renegotiate` can drop VBUS or reset the controller; its README lists the tools |

The pinned tags are the versions tested here; keep the `tps25751-bench` tag equal to the
library release that carries the console the firmware compiles in. Both servers claim a serial port or the meter, so only one session
should run them at a time. The Claude Code plugin `plugins/tps25751-bench`, from the
marketplace in `.claude-plugin/marketplace.json`, holds the `tps25751-pd-verify` skill and
the `pd-capture-analyst` agent that drive them with `usb-pd-docs`.

To work on a server itself, point the agent at the sibling checkout instead of the tag:

```bash
claude mcp add km003c --scope local -- uv --directory ../km003c-mcp run km003c-mcp
codex -c 'mcp_servers.km003c.command="uv"' \
  -c 'mcp_servers.km003c.args=["--directory","../km003c-mcp","run","km003c-mcp"]'
```

A local-scope entry should take precedence over the project's `km003c` entry in Claude
Code (not yet confirmed). Remove it with `claude mcp remove km003c --scope local`.

## Rules for every change

Platform, from [CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md):

- No RTTI. `dynamic_cast` and `typeid` do not compile; use `static_cast` on an object
  whose address you chose, or `readRegisterAs<T>()`.
- No `static_assert(false, ...)` in a template body.
- Every string literal passed to a `Stream` goes in `F()`.
- Create registers through the factory or the typed reads, keyed on
  `TPS25751Registers::Address`. There is no separate type enum.

The controller's protocol:

- The first byte of every register read is a length byte, not data, and a write sends
  one. Only the private `TPS25751::readRegister()` and `writeRegister()` frame a
  transaction.
- A PPS APDO uses 100 mV and 50 mA units; the other PDO types use 50 mV and 10 mA. Decode
  through `TPS25751Pdo`, never with shifts in a register class.
- The firmware sets bits the TRM marks reserved and reports length bytes the TRM does not
  document. Check hardware before adding a reserved-bit rule, and start every write from
  a fresh read.

## Components

The register classes are indexed in [include/AGENTS.md](include/AGENTS.md). The other
components, each documented in its header:

| Component | Role | Background |
|---|---|---|
| `TPS25751` | Owns the bus and address; typed reads and writes for all 37 registers; `readRegisterRaw()`; `executeCommand()`, `issueCommand()`, `writeCommandData()`, `burstWrite()` | ARCHITECTURE.md, Layer Responsibilities |
| `TPS25751Register` | Base class: byte buffer, `extractBits*()`, `setBits()`, the first two validation tiers | ADR-012 |
| `TPS25751RegisterFactoryImpl`, `TPS25751Factory` | Create a register class from an `Address` | STANDARDS.md, Wiring a new register in |
| `TPS25751Pdo`, `TPS25751Rdo` | Value types for one PDO or RDO word; `validatePdoList()` | ADR-013 |
| `TPS25751DownstreamDevice` | Register access to a part on the I2Cc bus through `I2Cr` and `I2Cw`, or directly on a `TwoWire` | ADR-007, ADR-010 |
| `TPS25751PatchLoader` | Loads a firmware and configuration image into a controller in PTCH (TI SLVAFV8A) | ADR-011; CONSTRAINTS.md, Patch Burst Mode |
| `TPS25751EepromUpdater` on `TPS25751CommandExecutor` | Updates, reads back, and blank-programs the configuration EEPROM (TI SLVAFL1) | ADR-014; CONSTRAINTS.md, Flash tasks and EEPROM update |
| `TPS25751BenchConsole` on `TPS25751BenchProtocol` | Answers a line protocol on a `Stream`: raw and decoded register reads, a snapshot, allowlisted tasks and writes, and restarts. Not included by `TPS25751.h` | ADR-015; tools/pd-bench/README.md |

The typed driver for the BQ25798 charger lives in the sibling `lib/BQ25798` repository
and depends on this library; nothing here depends on it.

## 4CC Command-Task Interface

The host writes input bytes to DATA (0x09), writes a 4-character task code to COMMAND
(0x08), and polls COMMAND until the controller clears it or writes `"!CMD"` for a code it
rejects. Results come back in DATA, with the task's return code in `DATA[0]`.
`TPS25751::executeCommand()` runs that handshake for any task. These rules cause silent
failures when missed; CONSTRAINTS.md (4CC Command Interface) gives the evidence for each:

- `I2Cr` results start at `DATA[1]`. One `I2Cr` returns at most 63 bytes.
- `I2Cw` is `{addr, Length, regOffset, payload}`, not a mirror of `I2Cr`'s
  `{addr, regOffset, numBytes}`. `Length` counts the register-offset byte, so it is the
  payload length plus 1, and the payload is at most 11 bytes. A longer payload is
  truncated without an error.
- The target address is 7 bits; bit 7 must be 0.
- The TRM asks for 5 s between two `I2Cr`, or two `I2Cw`, commands.
  `TPS25751DownstreamDevice` warns about it; `executeCommand()` does not.
- `FLrd` data starts at `DATA[0]` and has no return code.
- After `PBMc` clears COMMAND, wait 20 ms before reading DATA. Use `issueCommand()`,
  which reads no DATA.
- BOOT_FLAGS stays stale until a restart. Choose an EEPROM region from the region
  pointers, and never write a pointer before the region has been compared.

## Build and test

Run these from the library root. Each example has its own environment; `pio project
config` lists them.

```bash
pio test -e native
```

```bash
pio run -e example-read-registers
```

```bash
pio run -e example-read-registers -t upload
```

`pio test` without `-e native` targets the hardware environments and tries to upload.
A green native run is required for every change. The serial monitor runs at 115200 baud.

The bench server has its own tests, which use a fake serial port and never open a real
one:

```bash
cd tools/pd-bench && uv run --locked pytest
```

## Git Workflow

Every change follows this workflow. A push to `main` publishes to the PlatformIO
Registry, so git hooks enforce the workflow locally and CI checks it again. The
enforcement rules, the tooling an agent may not edit, and the release process are in
[scripts/AGENTS.md](scripts/AGENTS.md).

A new clone needs `scripts/setup_hooks.sh` run once by the human. Claude Code and Codex
warn at session start when the hooks are not active.

1. Work on a topic branch, never on `main`. Name it `<type>/<kebab-topic>`, where type is
   one of `feat fix docs refactor test chore perf build ci`.
2. Write commit subjects as Conventional Commits, `type(scope)!: subject`, with the same
   type list, at most 100 characters, imperative mood, and no attribution trailers. One
   logical change per commit.
3. When the branch is done, rebase it onto `main`, commit or stash everything so the tree
   is clean, untracked files included, and run `scripts/verify.py`. A pass records the
   branch tip as verified.
4. Run `scripts/integrate.py <branch>` from the checkout that has `main`. It merges with
   `--no-ff`, tags the tip `archive/<branch>`, and deletes the branch. Agents may run
   this step, after the human confirms the branch is finished.
5. The human pushes: `git push origin main refs/tags/archive/<branch>`. Agents never
   push.
6. CI on `main` runs the gate again, bumps the version, tags, and publishes. Never edit
   the version fields in `library.json` or `library.properties` by hand.

Agents never pass `--no-verify`, change `core.hooksPath`, or commit, amend, reset, or
fast-forward on `main`. If a refused fast-forward leaves files staged on `main`, restore
the tree with `git reset --merge HEAD`.
