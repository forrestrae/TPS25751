# Plan: KM003C USB PD analyzer toolset for TPS25751 development

Status: approved 2026-11-22 (draft 2, with your answers to the ten open questions
recorded under "Decisions recorded" at the end). Phase 0 complete 2026-11-22: the plan
(`docs/km003c-toolset-plan`) and `chore/drop-cursor-vscode` are integrated, with the Codex
row in `AGENTS.md` marked planned until Phase 8. Phase 1 complete 2026-11-22: S0 to S4
ran (S5 was not needed); the results and the decisions they fix are under "Phase 1
results" at the end. Phase 2 complete 2026-11-22: the public fork
`forrestrae/mcp-powerz-km003c` is cloned at `lib/km003c-mcp` (push to `upstream`
disabled), with worktree `lib/km003c-mcp-work`, and its workflow (hooks, `scripts/verify.sh`,
`AGENTS.md`) is merged into the fork's local `main`. Next: Phase 3 in the fork.

Phase 3 branch layout, decided at its start: the upstream items form one linear stack cut
from `upstream/main` (`upstream/macos-backend`, `upstream/disconnect-reply`,
`upstream/pd-monitor-enable`, `upstream/pd-poll-interval`, `upstream/busy-error`,
`upstream/idle-release`, `upstream/trigger-gate`), because later items edit the same
files as earlier ones. Each upstream branch keeps upstream's current defaults. Fork-only
branches follow on top: `feat/pd-status-during-capture` (item 6) and `feat/fork-defaults`
(idle release on at 2 s, trigger tools off unless `KM003C_ENABLE_TRIGGER=1`). They merge
into the fork's `main` in that order with `--no-ff`.

Phase 3 complete 2026-11-23: all of the above, plus `upstream/disconnect-reply` and the
fork-only `docs/upstream-branch-verify` and `fix/verify-dev-extras`, are merged into the
fork's local `main` after an independent review and a re-review of its fixes. The
`upstream/*` branches remain as pull-request heads. On the fork's `main`: 282 unit tests
pass, and the hardware tests pass 9 of 9 (claim and release 10 times at about 1 ms per
release, a PD capture during a 1000 SPS stream with 0 of 2306 samples dropped, the busy
error naming the holder). Next: Phase 4 in the fork.

Phase 3 facts and known limits, for later phases:
- A second EnablePdMonitor returns Reject only when nothing is sent in between; after a
  Connect it returns Accept again. `enable_pd_monitor()` sends Connect first, so it gets
  Accept on this meter, and treats Reject as non-fatal for other firmware.
- Each enable attempt waits at most 500 ms per request, so the device lock is held for
  about 1 s at most. A meter slower than that would make `pd_capture_start` fail with
  `PdMonitorNotEnabledError`; this meter answers in 3 to 17 ms.
- `device_release` refuses while any lease is held (a capture, a stream, or another call
  in flight), rather than releasing at once.
- The USB-reset recovery for a late-answering meter is unit-tested only; the fault has not
  recurred on hardware.
- A corrupt meter reply to `pd_status` or `measure` raises an unhandled `ProtocolError`
  instead of returning an error result (upstream behavior). Phase 4 handles it, since it
  reworks the decode path.
- `KM003C_ENABLE_TRIGGER`: unset gives the default (registered upstream, absent in the
  fork), `1` registers the tools, and any other value leaves them out.

Phase 4 complete 2026-11-23: `upstream/pd-capture-files` (cut from `upstream/trigger-gate`,
offerable upstream) and the fork branch that merges it into the fork's code are merged
into the fork's local `main` after an independent review. On the fork's `main`, 399 unit
tests pass, and the hardware tests pass 10 of 10, with the interactive one skipped. The
cold-attach hardware test ran with the human through `km003c-capture record --stop-on
contract --settle-ms 4000`. It stopped on `contract`, with the header recording firmware
2.0.0, and recorded the same 16 messages as Phase 1; that file is now a test fixture.
Next: Phases 5 and 6 in this library, which may run in parallel.

Phase 4 corrections and facts, for later phases:
- Connection events carry a 32-bit timestamp in bytes 1 to 4, like PD messages; the
  24-bit wrap in the Phase 4 section (from the protocol-research docs) is wrong. At
  about 40.1 million ms of uptime, a connection event read `450eef630211`: 40103694 ms,
  code 0x11. The reader uses the timestamps as stored.
- `pd_capture_read` pages by position, not time: `messages` and `full` take `after` (a
  line number in the file, exclusive) and return `next_after`, absent on the last page;
  `since` is an inclusive meter-time window. Phase 7's `since=<last t>` becomes
  `after=<next_after>`.
- `facts` is bounded to about 8 KB. It always keeps the final contract's Request and the
  Source_Capabilities it named, and the latest Discover Identity ACK per SOP and sender,
  and it reports what it omitted.
- Phase 7's sanity rule "only `conn` records: PD monitoring was not active" does not hold
  on firmware 2.0.0, which captures with the monitor disabled. There, only connection
  records more likely means no PD traffic (for example a non-PD source). The `summary`
  warning quotes the header's `pd_monitor` and should be read that way.
- A retransmitted message (same bytes, sender, and SOP with no GoodCRC between) appears as
  "(retry)" in `summary` and counts once toward `stop_after_messages`, the footer, and
  `facts`.
- The server writes the footer on SIGTERM (reason `manual`). `km003c-capture record`
  exits 0 on its stop condition, 3 when it timed out before one, and 2 for a file it
  cannot read.
- `settle_ms` and `status_interval_ms` run on meter time; `timeout_s` runs on host time.

Correction found in Phase 0, affecting Phase 5: since `ci/verify-launcher`, `verify.py`
is a launcher that runs `main`'s copy of `scripts/verify_gate.py`, and agents may edit
`verify_gate.py` on a topic branch. The two Phase 5 checks therefore go into
`verify_gate.py` on `build/verify-pd-bench-checks` without a `WORKFLOW_GUARD_OFF=1`
session, and they take effect for branches verified after that branch is pushed.

Supported agents: Claude Code and Codex only. Cursor, VS Code, and Windsurf are dropped
(Phase 0).

## Goal and shape

An agent working on TPS25751 code, in this library or in a project that consumes it,
can measure VBUS and IBUS, capture and decode a USB PD negotiation, cause a fresh
negotiation without a human, and compare the wire with the TPS25751's registers.

The toolset has five parts:

| Part | Where | What |
|---|---|---|
| `km003c` MCP server | Fork of `fejesd/mcp-powerz-km003c`, cloned at `lib/km003c-mcp` | Meter access: ADC, streaming, PD capture to JSONL files with bounded read modes, gated trigger tools. Generic; knows nothing about the TPS25751. |
| Bench console firmware | This library: `TPS25751BenchConsole` class plus `examples/pd-bench` | Line protocol on a Teensy USB serial port: register reads, 4CC tasks, a cross-check snapshot. |
| `tps25751-bench` MCP server | This library: `tools/pd-bench/` (Python, uv project, excluded from the PlatformIO package) | Host side of the console: port discovery, exclusive open per call, typed tools. Versioned with the firmware it talks to. |
| `tps25751-pd-verify` skill | This library: one `SKILL.md` in the Claude Code plugin `plugins/tps25751-bench/`, mirrored to `.agents/skills/tps25751-pd-verify/` for Codex | The verification workflow and the cross-check rules. |
| `pd-capture-analyst` agent | The same plugin; Claude Code only | Reads large captures so the main session does not. Under Codex the skill pages through captures itself. |

Keeping the meter server generic is what makes most of the fork's changes candidates for
upstream, and keeping the bench server next to the firmware means the protocol on both
ends always comes from the same tag.

## What the reading established

These facts shape the design; the spike settles the rest.

- **The fork never enables PD monitoring.** `Recorder.start_pd()` only polls GetData
  with attribute 0x10. Its hardware test `test_hw_pd_capture_idle` checks only that no
  error occurs, so it never proved that PD messages arrive.
- **The fork claims the interface at first use and holds it** until process exit
  (`Km003cService.device()` caches the session; only `shutdown()` and transport errors
  drop it).
- **The fork polls every 50 ms** (`Recorder(poll_interval_s=0.05)`), 20 ms only for
  1000 SPS streams.
- **The fork already decodes the legacy attach codes** 0x11 and 0x12
  (`PD_EVENT_CONNECT = (0x21, 0x11)`), and reads firmware and hardware versions through
  AES memory reads (`device_info`). That gives the spike the meter firmware version that
  the notes lack.
- **The fork's `pd_capture_read` stores full decodes in memory** and pages them out,
  with `decode=False` as the only size control.
- **Trigger discovery** filters `serial.tools.list_ports.comports()` by VID 0x5FC9 and
  PID 0x0063, not by globbing `/dev/cu.usbmodem*`. On this Mac today the meter (serial
  `053893`) is `/dev/cu.usbmodem0538932` and the Teensy (VID 0x16C0, PID 0x0483, serial
  `18176430`) is `/dev/cu.usbmodem181764301`, so VID/PID filtering is required.
- **Homebrew libusb is present** at `/opt/homebrew/lib/libusb-1.0.dylib`; `uv` and
  `uvx` are installed; system Python is 3.15.
- **The captured Discover Identity ACK in the notes was sent by the TPS25751 board, not
  the charger.** Header 0x428F has power role Sink and data role UFP, and its ID Header
  VDO carries VID 0x0451. It is the board's own identity, so it does not cross-check
  0x48, which holds the identity the board received from its partner. A 0x48
  cross-check needs a capture in which the partner sends a Discover Identity ACK on SOP.
- **The Apple 140 W Source_Capabilities on the wire match the bench words already in
  `test_TPS25751Pdo.cpp`** (`kRx140W` = 0x0981912C, 0x0002D12C, 0x0004B12C,
  0x000641D6), and the Request RDO 0x4184B12C matches `test_TPS25751ActiveRDOContract`.
- **The `tps25751-docs` server serves registers, not 4CC tasks.** The task list below
  comes from TRM SLVUCR8B section 4 (pages 61 to 86), read from the PDF in the server
  repo. A follow-up could add the 4CC tasks to that server; it is not part of this
  plan.

### Codex 0.171 (checked 2026-11-22)

From the Codex configuration reference and MCP pages, and from strings in the installed
`codex-cli 0.171.0` binary. A `codex mcp list` probe in a scratch project did not show a
project-defined server, but that subcommand may read only the user file, so the probe
settles nothing; Phase 8 confirms in this repository.

- **Project config exists, for trusted projects only.** Codex walks from the project
  root (the directory holding `.git`) to the working directory and loads every
  `.codex/config.toml`, the closest winning. The MCP page says servers can be scoped to
  a project with `.codex/config.toml`, trusted projects only. This library is already
  trusted in `~/.codex/config.toml`. Trusting a project also lets its MCP servers run
  outside the Codex sandbox, which the meter's libusb access and the Teensy's serial
  port need.
- **Codex has no MCP server for this library today.** `~/.codex/config.toml` defines
  neither `tps25751-docs` nor `usb-pd-docs`, and no project file exists, so the new file
  also gives Codex the two docs servers.
- **Per-tool approval exists.** `[mcp_servers.<server>]` takes
  `default_tools_approval_mode`, and `[mcp_servers.<server>.tools.<tool>]` takes
  `approval_mode`; both accept `auto`, `prompt`, `writes`, or `approve`. The docs define
  only `writes` (prompts for tools not marked read-only); `prompt` presumably always
  asks. Servers also take `enabled_tools` and `disabled_tools` lists.
- **Approval is weaker than in Claude Code.** The docs do not say whether `prompt`
  survives `approval_policy = "never"`, and `--dangerously-bypass-approvals-and-sandbox`
  skips all prompts. Your `~/.codex/config.toml` sets
  `approvals_reviewer = "auto_review"`, which routes approval requests to an automatic
  reviewer instead of you. Under Codex, therefore, the `KM003C_ENABLE_TRIGGER` flag is
  the only trigger guard to rely on; `approval_mode = "prompt"` is a second layer that
  Phase 9 tests.
- **Skills:** the binary lists `.agents/skills` among its skill locations, beside the
  Codex-home and user skill directories, consistent with your instruction.

## Rev B 4CC tasks and register writes that start a negotiation

From SLVUCR8B. Rev B has **no task that sends a Hard Reset**; nothing below sends one.

| Stimulus | TRM | Role | Wire traffic | VBUS | Notes |
|---|---|---|---|---|---|
| `GSrC` | 4.3.6, p.65 | Sink | Get_Source_Cap, Source_Capabilities, Request, Accept, PS_RDY | Stays at the contract voltage if the same PDO is chosen, otherwise steps to the new one | The least disruptive way to get fresh Source_Capabilities and a Request. Updates 0x30. Section 2.3 names it as the way to renegotiate after changing 0x33 or 0x37. |
| `ANeg` | 4.6.1, p.83 | Sink | Request, Accept, PS_RDY, only if the re-evaluated RDO differs from the active one | Steps between PDO voltages | To force traffic, change 0x37 (for example the maximum voltage to 9 V), run `ANeg`, restore 0x37, run `ANeg` again. Exercises 0x37 and 0x35 but sends no Source_Capabilities. |
| `SSrC` | 4.3.8, p.72 | Source | Source_Capabilities from the board, then the partner's Request | Per the new contract | Rejected unless the board is Source. In scope: the board can source. The source-role counterpart of `GSrC`, for runs with the board sourcing an iPhone or the meter. |
| `GSkC` | 4.3.5, p.65 | Either | Get_Sink_Cap, Sink_Capabilities | None | Not a renegotiation. Updates 0x31. |
| Write 0x28 PORT_CONFIGURATION or 0x27 GLOBAL_SYSTEM_CONFIGURATION | 3.2.12, 3.2.11 | Either | Type-C detach and reattach, then the full sequence including Discover Identity | Drops to vSafe0V and returns | "Any modifications to this register will cause a port disconnect and reconnect." The closest software equivalent of a cold attach. Unknown whether rewriting the same value counts as a modification; Phase 6 tests it. A 0x27 write must start from a fresh read (AGENTS.md). |
| `Gaid` (warm) / `GAID` (cold) | 4.2.1, 4.2.2, p.62 | Either | Type-C Error Recovery for about 1 s, restart, fresh attach | Drops to vSafe0V and returns | Host-written registers revert to the AppConfig. `GAID` reboots through the OTP bootloader, so an EEPROM-less board comes back in PTCH and needs a patch reload; the console does that reload with the existing `TPS25751PatchLoader::loadPatchBundle()` (Phase 6). I2C NAKs during the reboot. |
| `SWSk`, `SWSr`, `SWDF`, `SWUF` | 4.3.1 to 4.3.4 | Either | PR_Swap or DR_Swap | PR_Swap moves the VBUS source | Out of scope for the first version; the console refuses them unless built with a flag. |

The stimuli that drop VBUS (0x27, 0x28, `Gaid`, `GAID`) cut power only to what runs
from VBUS. On the bench the board runs from a battery, so the TPS25751 and the Teensy
stay up and the console keeps its serial port through every stimulus. Phase 6 confirms
this on the first VBUS-dropping run (`HELLO` uptime must keep counting). A downstream
board that runs from VBUS would lose the console; the skill then offers only `gsrc`,
`aneg`, and `ssrc` (Phase 7).

## Where the work happens: worktrees

All implementation runs in git worktrees, never in the main checkouts. The main
checkout of this library, `lib/TPS25751`, stays on `main` and is used only to integrate.

| Repository | Main checkout (stays on `main`) | Implementation worktree |
|---|---|---|
| This library | `lib/TPS25751` | `lib/TPS25751-km003c` |
| The fork (from Phase 2) | `lib/km003c-mcp` | `lib/km003c-mcp-work` |

**Why siblings in `lib/`.** The library's `.mcp.json` reaches the docs server through
`../device-register-mcp-server`, and the local override of Phase 8 reaches the fork
through `../km003c-mcp`. A worktree beside the main checkout resolves both paths exactly
as the main checkout does. Claude Code's own worktree option (`--worktree` or
`EnterWorktree`) puts the worktree under `.claude/worktrees/` inside the repository,
where those relative paths do not resolve, so it is not used. `lib/` has no PlatformIO
project above it, so a second copy of the library there is not picked up as a
dependency.

**Creating the library worktree and moving the plan into it.** You do this before
launching the orchestrator, so the orchestrator starts inside the worktree (see
"Execution: models, effort, and orchestration"). This file and its kickoff prompt are
untracked in the main checkout; they move into the worktree at the same paths and are
committed there in Phase 0, so the main checkout holds no trace of them. From the
project root, `TPS25751-i2ct-test`:

```bash
git -C lib/TPS25751 worktree add ../TPS25751-km003c -b docs/km003c-toolset-plan main
```

```bash
mv lib/TPS25751/docs/plans/km003c-pd-analyzer-toolset.md lib/TPS25751/docs/plans/km003c-orchestrator-kickoff.md lib/TPS25751-km003c/docs/plans/
```

From then on the worktree copies are the only copies; the Phase 1 results are appended
to this file there.

**How the library's workflow runs across the two.**

- Topic branches are created and checked out in the worktree, one at a time, each from
  the current `main`.
- `scripts/verify.py` runs in the worktree. Its verified-commit stamps live in the
  shared git directory, so `integrate.py` sees them from the main checkout.
- `scripts/integrate.py <branch>` runs in the main checkout, because it refuses to run
  anywhere but the worktree that has `main` checked out. It deletes the merged branch,
  and git refuses to delete a branch checked out in another worktree, so the worktree
  first runs `git switch --detach`.
- The hooks need nothing new: `core.hooksPath` is `.githooks`, which each worktree
  resolves to its own copy, and the agent guard finds the worktree's root through
  `git rev-parse --show-toplevel`.
- Codex trust is per path. `lib/TPS25751-km003c` is not yet trusted, so Codex skips its
  `.codex/` hooks and, later, its `.codex/config.toml` until you trust it and approve
  its hooks, as `AGENTS.md` describes for the main checkout.
- Agent sessions for this work start in the worktree, not in `lib/TPS25751`.

**The fork** follows the same pattern from Phase 2: the clone at `lib/km003c-mcp` stays
on `main` (it is what the local override runs), and its topic branches are worked in
`lib/km003c-mcp-work`, created with
`git -C lib/km003c-mcp worktree add ../km003c-mcp-work -b <branch> main`.

**Removal.** When the last phase is integrated, `git -C lib/TPS25751 worktree remove
../TPS25751-km003c` (and the same for the fork). I propose it and you confirm; nothing
is removed while it holds uncommitted work.

## Execution: models, effort, and orchestration

One Claude Code orchestrator session runs the plan from start to end. It does the
interactive work itself (everything that needs you at the bench or a decision from you)
and hands each phase's code to a subagent with the model and effort below, then reviews,
verifies, and integrates. Subagents cannot talk to you, which is why every hardware step
stays with the orchestrator.

| Work | Runs in | Model | Effort | Why |
|---|---|---|---|---|
| Orchestrator | Main session | Opus 5.5 (`opus`) | `high` | Long-horizon coordination, review of every phase, and the hardware judgment calls. Fable 5.2 costs twice as much per token, and the orchestrator accumulates the most tokens of any session over days. `high` because Opus 5.5 defaults to `medium`, which is too low for this. |
| Phase 0 | Subagent | Sonnet 5.5 (`sonnet`) | `medium` | Mechanical: commit, delete two files, edit two documents. |
| Phase 1 spike | Orchestrator itself | | | Interactive: cold attaches with you, and reading the results decides the poll rate and enable sequence. The script is short. |
| Phase 2 | Subagent | Sonnet 5.5 | `medium` | Fork, clone, worktree, and small copies of existing hook tools. |
| Phase 3 | Subagent | Opus 5.5 | `high` | Lease and idle-release timer across threads, retry sequences, error paths: easy to get subtly wrong. |
| Phase 4 | Subagent | Opus 5.5 | `high` | File format, PD header decode, stop conditions, timestamp wrap, bounded outputs. |
| Phase 5 fixtures | Subagent | Opus 5.5 | `high` | Bit-exact assertions written by hand from the `usb-pd-docs` tables; a wrong field here hides decode bugs. |
| Phase 5 `verify.py` change | A `WORKFLOW_GUARD_OFF=1` session you start | Sonnet 5.5 | `medium` | Two added steps; the orchestrator writes the exact change for that session. |
| Phase 6 code | Subagent | Fable 5.2 (`fable`) | `high` | The largest and most cross-cutting phase: embedded C++ under the library's standards, a protocol on both ends, 4CC and patch-reload semantics, serial ownership. Fable 5.2 guidance is to start at `high`. |
| Phase 6 hardware run | Orchestrator itself | | | Drops VBUS and needs you to swap boards. |
| Phase 7 skill | Subagent | Opus 5.5 | `high` | Agent-facing instructions and the cross-check rules, which both agents must follow exactly. |
| Phase 7 end-to-end runs | Orchestrator, plus a Codex session you drive | Your Codex default | | The Codex runs need you at the approval prompts. |
| Phases 8 and 9 | Subagent | Sonnet 5.5 | `medium` | Configuration, documentation, permission lists; you apply the guarded files. |
| Phase 10 drafts | Subagent | Opus 5.5 | `medium` | Issue and pull request text, which you approve before anything is opened. |
| Review before each integration of Phases 3 to 7 | Fresh subagent | Opus 5.5 | `high` | An independent read of the diff and the plan section, without the implementer's conclusions. |
| `pd-capture-analyst` (a product of Phase 7) | Its frontmatter | Sonnet 5.5 | `medium` | Reading-heavy with a bounded 30-row output. |

If a phase stalls on reasoning rather than missing information, the orchestrator reruns
it one step up (Sonnet to Opus, Opus to Fable) rather than raising effort to `xhigh` or
`max`. You can do the same for the orchestrator itself with `/model fable` during a hard
hardware session and switch back afterwards.

**Rules the orchestrator follows**

- It starts in `lib/TPS25751-km003c` with `lib/` added, so it reaches the main checkout
  (for `integrate.py`) and the fork's directories.
- One writer per worktree: never two subagents in the same worktree at once. A fork
  subagent and a library subagent may run at the same time, since they use different
  worktrees.
- Never the Agent tool's `isolation: "worktree"`, which creates worktrees under
  `.claude/worktrees/` (see "Where the work happens").
- Subagents commit on their topic branch and stop. The orchestrator reviews, runs
  `scripts/verify.py` in the worktree, detaches the worktree, and runs
  `scripts/integrate.py` from `lib/TPS25751`. You push.
- It stops and asks you before: each hardware step, any VBUS-dropping stimulus, any
  push-dependent step (Phase 8 pins tags that exist only after you push), anything
  upstream, and any change to guarded files (`.claude/settings*.json`, `.codex/`,
  `scripts/` workflow tooling), which it prepares as a patch for you to apply.
- It updates this plan's Status line at the end of each phase, in that phase's branch.
- It does not run with permissions bypassed: the `ask` rules on the bench tools only
  work with prompts on.

**Launching it.** `docs/plans/km003c-orchestrator-kickoff.md` holds the kickoff prompt
and nothing else.

1. Create the worktree and move the two plan files into it (see "Where the work
   happens").
2. If you will use Codex in the worktree, trust `lib/TPS25751-km003c` and approve its
   hooks, as `AGENTS.md` describes.
3. From `lib/TPS25751-km003c`, start the orchestrator:

   ```bash
   claude --model opus --effort high --add-dir .. -n km003c-orchestrator "$(cat docs/plans/km003c-orchestrator-kickoff.md)"
   ```

   In the desktop app instead: new session on folder `lib/TPS25751-km003c`, add folder
   `lib/`, model Opus 5.5, effort High, and paste the file's contents as the first
   message.

The session should read the plan and start Phase 0 without asking anything further.

## Phases

Phases are in dependency order. Phases 5 and 6 can run in parallel once Phase 4's file
format is fixed. Phase 0 is independent of the rest and can land any time.

### Phase 0: Commit the plan; drop Cursor and VS Code

Runs in the library worktree. Nothing in this phase or later starts until the worktree
exists and this file has moved into it (see "Where the work happens").

0. You create `lib/TPS25751-km003c` on branch `docs/km003c-toolset-plan` and move this
   file and the kickoff prompt into it, before launching the orchestrator.
1. `docs/km003c-toolset-plan`: commit both files there, then detach the worktree and
   integrate from the main checkout.
2. `chore/drop-cursor-vscode`:
   - Delete `.cursor/mcp.json` and `.vscode/mcp.json` (each directory holds only that
     file).
   - Keep `.cursor` and `.vscode` in `export.exclude` in `library.json`: the PlatformIO
     IDE extension creates `.vscode/` on its own, and the entries keep such files out of
     the published package.
   - `AGENTS.md`: replace the per-tool table with two rows, Claude Code (`.mcp.json`
     plus `.claude/settings.json`) and Codex (`.codex/config.toml`, trusted project).
     The Windsurf row goes too, since only Claude Code and Codex are supported. Remove
     the two lines from the repository tree.
   - `docs/AGENTS.md`: drop the `.cursor/mcp.json` and `.vscode/mcp.json` mentions.

The Codex row describes a file that does not exist until you apply it in Phase 8. If you
prefer, the row says "planned" until then.

**Verification:** `git grep -nE '\.cursor|\.vscode|Cursor|VS Code|Windsurf'` finds
nothing outside `docs/plans/` and the two `library.json` exclusions;
`scripts/verify.py` passes.

**Needs from you:** nothing; review the merges.

### Phase 1: Hardware spike

Settles enable, poll rate, and macOS behaviour before any design depends on them. Runs
from a throwaway clone of the upstream repository in the session scratchpad; no
repository is created and nothing is committed.

**Deliverables**

- `spike.py`, a script beside the clone that imports the fork's package and adds what
  the fork lacks: DisablePdMonitor, a confirmed EnablePdMonitor (5 attempts, Connect and
  200 ms between them, as in `km003c.py`), a configurable poll interval, and JSONL
  logging of every poll's raw payload.
- A results section appended to this plan, recording what each test showed and the
  decisions it fixes.

**Tests, in order**

| ID | Needs you | What it does | Settles |
|---|---|---|---|
| S0 | No | `uv sync` the upstream clone. Load the `libusb_package` backend on arm64; if it fails, retry with `KM003C_LIBUSB_DLL=/opt/homebrew/lib/libusb-1.0.dylib`. Run the fork's hardware tests except the two trigger tests (`KM003C_HW=1 pytest tests/test_hw.py -k "not trigger"`). Record `device_info`: firmware and hardware versions. List trigger ports by VID/PID without opening them. | macOS backend, interface claim, CDC discovery, meter firmware version |
| S1 | No | Claim, Connect, release, reclaim, 10 times, timing each claim. Send EnablePdMonitor, release, reclaim, and check whether the monitor is still enabled (by whether a later capture needs it). With a second process holding the interface, try to claim and record the exact pyusb error, and whether `ioreg -l` names the holder (a `UsbExclusiveOwner` property on the interface). | Idle-release cost, whether enable survives a release, the busy error text |
| S2 | One cold attach | Send DisablePdMonitor, then capture 30 s at 50 ms polling **without** enabling. | Whether this meter needs EnablePdMonitor |
| S3 | One cold attach | Confirmed enable, capture 30 s at 15 ms. This is the reference event list. | Baseline |
| S4 | One cold attach | Confirmed enable, capture 30 s at 1000 ms. Compare with S3 by message type, SOP, and MessageID sequence; a gap in MessageID per SOP and sender is a lost message. | Whether the meter buffers a whole burst. If S4 loses nothing, 50 ms is safe with a wide margin. |
| S5 | One cold attach, only if S4 lost events | Confirmed enable, capture at 50 ms, compare with S3. | Whether 50 ms is enough or the default must be 15 to 20 ms |

S2 runs before any enable in the session and sends DisablePdMonitor first, because the
meter may still be enabled from the earlier session.

**What you do**

Cabling for every attach: charger into the meter's receptacle (input), meter's plug into
the TPS25751 board (sink). The meter's own host USB cable stays connected throughout.
The board stays on its battery and is never switched off.

For each of S2 to S4 (and S5 if needed):

1. When I say **"unplug"**, unplug the meter from both the charger and the board, and
   reply "done".
2. I start the capture and say **"attach now"**.
3. Plug the meter into the board first, then plug the charger into the meter. Reply
   "done".
4. I stop the capture after PS_RDY plus 5 s, or at 30 s.

Three or four cold attaches in all, about ten minutes. Close any other program using
the meter before S0.

**Verification:** the results themselves; S3 must show Source_Capabilities, Request,
Accept, and PS_RDY with the wire bytes in the notes' table, or the spike stops and we
look at why.

### Phase 2: Fork repository and its workflow

**Deliverables**

- Public GitHub fork `forrestrae/mcp-powerz-km003c`, which I create with
  `gh repo fork fejesd/mcp-powerz-km003c --clone=false` (you approved this). It keeps
  the upstream name so pull requests are simple. Cloned at `lib/km003c-mcp` with
  remotes `origin` (your fork) and `upstream`.
- A workflow for the fork, lighter than this library's but on the same rules:
  - Topic branches `<type>/<kebab-topic>`, Conventional Commits (upstream already uses
    them), one logical change per commit, no attribution trailers.
  - Changes meant for upstream go on branches cut from `upstream/main`, named
    `upstream/<topic>`, holding only that change. They merge into the fork's `main` like
    any other branch and stay available as clean pull-request heads.
  - `scripts/verify.sh`: `uv run pytest -m "not hw"` plus `ruff check`, on a clean tree.
  - `.githooks/commit-msg` (Conventional Commits regex) and `.githooks/pre-push`
    refusing pushes from agent shells (`CLAUDECODE`, the Codex variables,
    `WORKFLOW_AGENT=1`), with `scripts/setup_hooks.sh`. Smaller copies of this library's
    tools, not shared code.
  - `main` moves by `git merge --no-ff`. You push. You tag releases `vX.Y.Z`; the tag is
    what consuming projects pin.
  - `AGENTS.md` (with `CLAUDE.md` as a symlink) describing all of the above.

**Files:** new repository `lib/km003c-mcp`.

**Verification:** the fork's existing test suite passes unchanged on macOS; the hooks
reject a bad commit subject and an agent push.

**Needs from you:** run `scripts/setup_hooks.sh` once in the new clone.

### Phase 3: Core server changes

Each item is its own branch in the fork. Marked **U** if it goes on an `upstream/`
branch.

1. **Backend selection on macOS (U).** Try `KM003C_LIBUSB` (keep `KM003C_LIBUSB_DLL`
   as an alias), then `libusb_package`, then the system library through pyusb's normal
   search, which finds Homebrew's. Add a macOS section to the README. Exact fallback
   order comes from S0.
2. **Confirmed PD monitor enable (U)**, if S2 shows it is needed. `Km003cDevice`
   gains `enable_pd_monitor()` (Connect then EnablePdMonitor, up to 5 attempts, Connect
   and 200 ms between, `PdMonitorNotEnabledError` if never accepted) and
   `disable_pd_monitor()`. `start_pd` enables; `stop_pd` disables. If S2 shows the meter
   captures without it, enable anyway when the reply is Accept and treat a Reject as
   non-fatal, since other firmware may differ.
3. **Poll interval (U).** Default from S3 to S5 (15 ms unless S4 shows the buffer
   holds a burst), overridable with `KM003C_PD_POLL_MS`.
4. **Claim on demand, release when idle (U, offered as opt-in upstream; default here).**
   A `Lease` in `Km003cService`: each tool call takes a lease for its duration; a
   running capture or stream holds one until it stops. When the last lease ends, a timer
   releases the interface after `KM003C_IDLE_RELEASE_S` (default 2 s, so a loop of
   `measure` calls does not claim and release every time; 0 releases at once). Release
   sends DisablePdMonitor and StopGraph if active, Disconnect, then releases the
   interface. Reading a saved capture file never claims the meter. A new
   `device_release` tool releases at once.
5. **Busy error (U).** A failed claim raises `DeviceBusyError` whose message says
   another process holds the meter, names it when S1 showed that `ioreg` can (for
   example "pid 4242, python3.12"), and lists the likely holders: another agent
   session's `km003c` server with a capture running, the POWER-Z PC software, or a
   script.
6. **`pd_status` during a capture** returns the latest status block from the capture
   instead of refusing.
7. **Trigger gate (U, offered; default off here).** The trigger tools are registered
   only when `KM003C_ENABLE_TRIGGER=1`; otherwise they do not exist, so an agent cannot
   call them or see them. All of them are gated, `trigger_detect` included, because
   protocol probing can make a charger change VBUS. See Phase 9 for the cabling rules and
   permission settings.

**Tests (unit, fake transport):** enable answered by Disconnect then Accept; enable never
accepted; release after the idle timer, reopen on the next call; no release while a
capture runs; busy error text from a failing open; `pd_status` during a capture; trigger
tools absent without the flag and present with it.

**Tests (hardware, `KM003C_HW=1`):** enable accepted; claim and release 10 times; busy
error with a second process holding the meter; a 2 s idle capture with no errors.
Interactive tests that need a cold attach run only with `KM003C_HW_INTERACTIVE=1` and
prompt on the terminal.

**Needs from you:** nothing beyond reviewing merges and pushing.

### Phase 4: Capture files, read modes, stop conditions

**Capture file format** (`km003c-capture` version 1): JSON Lines, one object per line,
written and flushed as events arrive so a crash keeps everything before it.

| `type` | Fields |
|---|---|
| `header` | `format`, `version`, `started_utc`, `meter` {`serial`, `fw_version`, `hw_version`}, `server` {`version`, `poll_ms`}, `pd_monitor` {`enabled`, `attempts`}, `label`, `provenance` (free-form object supplied by the caller) |
| `status` | `t_host`, `t_meter_ms`, `vbus_mv`, `ibus_ma`, `cc1_mv`, `cc2_mv`, `raw` (the 12-byte status block). Written every `status_interval_ms` (default 100) and on every poll that carries events. |
| `pd` | `i`, `t_meter_ms`, `sop`, `wire` (message bytes), `raw_event` (size flag, timestamp, SOP byte, and wire), and a header summary: `msg`, `msg_id`, `power_role`, `data_role`, `spec_rev`, `num_objects`, `extended`. No full decode. |
| `conn` | `t_meter_ms`, `code`, `event` (`connect` or `disconnect`), `raw_event` |
| `mark` | `t_host`, `text`: an annotation such as "stimulus GSrC sent" |
| `error` | `t_host`, `kind`, `message` |
| `footer` | `stopped_utc`, `reason` (`messages`, `contract`, `timeout`, `manual`, `error`), counts by message type, VBUS min, max, first and last |

Full decodes are computed when a file is read, so decoder fixes apply to old captures
and files stay small. Every decoded value a tool returns sits beside the `wire` hex it
came from. Connection-event timestamps are 24-bit and wrap every 4.66 hours; the
reader extends them from the status blocks' 32-bit uptime.

Default location: an explicit `path` argument, else `$KM003C_CAPTURE_DIR`, else
`./captures` relative to the server's working directory. Every result returns the
absolute path. (With `uv --directory` the working directory is the server repository,
which is one reason Phase 8 recommends `uvx`.) `captures/` is gitignored in this library
and in each consuming project; only curated bundles are committed, under
`test/fixtures/` (Phase 5).

Under Codex, agents use the MCP tools for anything that touches the meter. The
`km003c-capture` command-line tool below would run inside the Codex sandbox, which may
block USB access; reading saved files with it is fine.

**Tool changes**

- `pd_capture_start(path?, label?, provenance?, stop_after_messages?, stop_on?,
  settle_ms=0, timeout_s=60, status_interval_ms=100)`. `stop_on` is `none`, `contract`
  (PS_RDY after an Accept of a Request), or `identity` (a Discover Identity ACK on SOP).
  `stop_after_messages` counts non-GoodCRC messages. `settle_ms` keeps capturing after
  the condition; Discover Identity came 3 s after PS_RDY in the notes' capture.
- `pd_capture_wait(timeout_s)`: blocks until the capture stops, then returns its
  summary. Agents wait in one call instead of polling.
- `pd_capture_mark(text)`.
- `pd_capture_stop()`: returns the summary.
- `pd_capture_read(path?, mode, sop?, types?, since?, limit?, include_goodcrc=False)`.
  `path` reads any saved file without touching the meter. Modes:
  - `summary` (default, bounded to about 2 KB): one line per non-GoodCRC message
    (`t=84002 SOP Request pos 4, 3.0 A [8210 2cb18441]`), counts, VBUS first, last, min,
    max, the final contract, and warnings, described under the skill.
  - `messages`: the same lines, filtered, paged.
  - `facts`: the objects a cross-check needs, each with its hex: Source_Capabilities
    PDOs, the Request RDO and its object position, Accept and PS_RDY times, and
    Discover Identity ACKs grouped by SOP and by sender (power and data role bits).
  - `full`: full decodes of the filtered messages, `limit` default 20.
- A command-line tool in the same package, `km003c-capture record|summarize|facts`,
  sharing the code. Used by the spike's successor tests, by fixture conversion, and by
  agents without MCP.

**Tests (unit):** a golden JSONL produced from PdPacket payloads built around the wire
bytes of the notes' Apple 140 W table, through the fake transport; each stop condition;
each read mode's output and size bound; timestamp wrap; reading a file with no meter
present.

**Tests (hardware):** one cold attach with `stop_on=contract`; check the file's header
records firmware and the footer's reason.

**Upstream:** all of it is generic (**U**), offered after it has run here for a while.

**Needs from you:** one cold attach for the hardware test.

### Phase 5: Captures as test fixtures (this library)

Branch `test/pd-capture-fixtures`.

**Layout**

```
test/fixtures/pd-captures/
  README.md                          index: one row per bundle
  2026-11-22-apple-140w-tps25751/
    capture.jsonl                    km003c-capture v1
    registers.json                   raw register bytes read right after the contract
    provenance.json                  charger, cable, sink board, meter serial and firmware,
                                     date, stimulus, and who made it
```

`registers.json` is what the bench `snapshot` command returns (Phase 6): register
address, length byte, and raw hex for 0x30 to 0x35, 0x37, and 0x48, plus STATUS and
ADC_RESULTS. A bundle therefore holds both sides of the cross-check: the wire and the
registers.

**Generator:** `test/support/pd_capture_fixture.py` (standard library only) reads a
bundle and writes `test/support/native/fixtures/PdCapture_<name>.h`: `constexpr` arrays
of the Source_Capabilities PDO words, the Request RDO, Discover Identity VDOs by sender,
and each register's raw bytes, with the provenance as a comment. Generated headers are
committed so `pio test -e native` needs no Python. `--check` fails if a header is stale.

**Unity tests:** new cases in `test_TPS25751Pdo`, `test_TPS25751Rdo`,
`test_TPS25751ReceivedSourceCaps`, `test_TPS25751ActivePDOContract`,
`test_TPS25751ActiveRDOContract`, and `test_TPS25751ReceivedSOPIdentity`. Each builds
the register class from the bundle's raw register bytes and asserts that it decodes to
the wire's words, bit for bit; each wire word's field values are asserted by hand from
the `usb-pd-docs` tables, not generated, so the generator cannot hide a decode error.

**First bundle:** the Apple 140 W capture, hand-converted from the notes' table into
`capture.jsonl` with `provenance.synthesized_from` naming the notes and meter firmware
"unknown", and `registers.json` built from the bench words already in
`test_TPS25751Pdo.cpp` and `test_TPS25751ActiveRDOContract.cpp`. It covers 0x30, 0x34,
and 0x35. It cannot cover 0x48 (see "What the reading established"). Phase 7 replaces
it with a real bundle made by the tools, from the same charger, and adds a 0x48 bundle
from an iPhone on the board's port, which fills 0x48 with no configuration change.

**Verification:** `pio test -e native`, `pd_capture_fixture.py --check`, then
`scripts/verify.py` and `scripts/integrate.py` as usual.

**`verify.py` change (approved).** `verify.py` gains two steps: `pd_capture_fixture.py
--check`, and the bench server's `uv run pytest` in `tools/pd-bench/` (from Phase 6).
`verify.py` is human-maintained workflow tooling, so the change goes on its own branch
`build/verify-pd-bench-checks`, made in a session you start with
`WORKFLOW_GUARD_OFF=1`, either by me or by you. Each step is skipped with a notice when
its directory does not exist yet, so the change can land before Phase 6.

### Phase 6: Bench console and bench server (this library)

Branch `feat/pd-bench`.

**Firmware**

- `include/TPS25751BenchProtocol.h`, `src/TPS25751BenchProtocol.cpp`: pure line parsing
  and reply formatting, compiled in `[env:native]` and unit-tested there.
- `include/TPS25751BenchConsole.h`, `src/TPS25751BenchConsole.cpp`: binds the protocol
  to a `TPS25751&` and a `Stream&`; the sketch calls `console.poll()` from `loop()`.
  Not included by `TPS25751.h`, so projects that don't use it pay nothing. Downstream
  firmware embeds the console with three lines rather than flashing our example.
- Patch reload: the console optionally takes a patch bundle (pointer and length, as
  `examples/load-patch-bundle` uses). After `GAID`, or whenever MODE reads PTCH, it
  reloads the patch with `TPS25751PatchLoader::loadPatchBundle()` and reports the
  outcome. A board with an EEPROM needs no bundle; a board without one and no bundle
  refuses `GAID`.
- `examples/pd-bench/src/main.cpp` and `[env:example-pd-bench]`, built with
  `-D USB_DUAL_SERIAL`: the console on `SerialUSB1`, human-readable logs on `Serial`.
  `pio device monitor` then watches the first port while the bench server owns the
  second, which removes the main reason the two would compete.

**Protocol (version 1)**

Requests are `@<id> <COMMAND> [args]`. Replies are lines prefixed `@<id> `, ending with
`@<id> OK` or `@<id> ERR <code> <text>`. Lines without the prefix are log output; the
host keeps them separately. Asynchronous events are `! <text>` lines.

| Command | Reply |
|---|---|
| `HELLO` | Protocol version, library version, build date, uptime, MODE, VERSION (0x0F), whether a patch bundle is compiled in, and the power role from STATUS |
| `RESTART <warm\|cold>` | Runs `Gaid` or `GAID`, waits for I2C to answer again, reloads the patch if MODE is PTCH, and reports each step |
| `RD <addr>` | Length byte and raw hex |
| `DEC <addr>` | Raw hex, then the library's `debugPrint()` output for that register |
| `SNAP` | Raw hex of 0x03, 0x14, 0x1A, 0x26, 0x30 to 0x35, 0x37, 0x3F, 0x40, 0x42, 0x48, 0x69, 0x6A, then clears INT_EVENT1 |
| `CMD <4cc> [hex]` | Task status, then the DATA output as hex. Allowed: `GSrC`, `GSkC`, `ANeg`, `SSrC`, `GPPI`, `MBRd`, `Gaid`, `GAID`, `SRDY`, `SRYR`, `DBfg`, `I2Cr`. Refused: `PBM*`, `GO2P`, `FL*`, `I2Cw`, and the swaps unless built with `-D TPS25751_BENCH_ALLOW_SWAPS`. |
| `WR <addr> <hex>` | Allowed only for 0x27, 0x28, 0x37, 0x42; reads back and returns the new raw hex |

`SNAP` is the cross-check channel. 0x30 to 0x35 hold the capabilities and the contract,
0x37 explains which PDO the board chose, 0x48 holds the partner's identity, STATUS
(0x1A), POWER_PATH_STATUS (0x26), POWER_STATUS (0x3F), PD_STATUS (0x40, including Hard
Reset details), and TYPEC_STATE (0x69) say what state the port is in, INT_EVENT1 (0x14)
says what happened since the last snapshot, and ADC_RESULTS (0x6A) gives the
controller's own VBUS reading to compare with the meter.

**Host server** (`tools/pd-bench/`, `uv` project, MCP SDK 2.x as in the docs server)

- Port discovery: Teensy VID 0x16C0 and PID 0x0483, narrowed by
  `TPS25751_BENCH_SERIAL` or `TPS25751_BENCH_PORT`. With dual serial, the console's
  port is the second interface; whether pyserial reports the interface number on macOS
  is checked in this phase, with `TPS25751_BENCH_PORT` as the fallback.
- Single owner: each tool call checks `lsof` on the port and, if another process has it
  open, fails naming that process (for example `pio device monitor`). It then opens the
  port with `exclusive=True`, sends `HELLO` if this is the first call, runs the command,
  and closes the port. Nothing holds the port between calls, so `pio run -t upload`
  works at any time. (`exclusive` uses `flock`, which `pio device monitor` ignores;
  hence the `lsof` check.)
- Optional `TPS25751_BENCH_LOG`: a file receiving all serial traffic, for you to
  `tail -f`.
- Tools: `bench_hello`, `bench_snapshot`, `bench_read(addr, decode=False)`,
  `bench_task(code, data_hex?)`, `bench_write(addr, hex)`, `bench_renegotiate(method)`.
  `bench_renegotiate` takes `gsrc` (sink), `aneg` (sink: changes 0x37, runs `ANeg`,
  restores it, runs `ANeg`), `ssrc` (source), `port_reset` (rewrites 0x28), `restart`
  (`RESTART warm`), or `restart_cold` (`RESTART cold`, with patch reload). It refuses a
  method that does not fit the board's current power role, and returns what it did and
  when, so the skill can put a mark in the capture.

**Exclusion from the package:** add `tools/` and `plugins/` to `export.exclude` in
`library.json`.

**Tests**

- Unit (native Unity): protocol parsing, reply formatting, allowlists.
- Unit (pytest, fake serial): framing with interleaved log lines and events, busy port,
  version mismatch.
- Hardware, run by an agent with the meter capturing: each `bench_renegotiate` method
  once, recording what crossed the wire and what VBUS did. This settles whether
  rewriting 0x28 with its current value disconnects the port, whether `GSrC` yields a
  full Request, Accept, PS_RDY sequence with the Apple charger, and whether `HELLO`
  uptime keeps counting through a VBUS drop (the console stays up on battery).
  `restart_cold` runs on an EEPROM-less board and must come back in APP. `ssrc` runs in
  the source session of Phase 7.

**Needs from you:** none for the code. The hardware run needs the board on its battery,
the charger attached, and an EEPROM-less board for `restart_cold`; I will say before any
method that drops VBUS, and before you need to swap boards.

### Phase 7: Verification skill and capture analyst

Branch `feat/pd-verify-skill`.

**Files**

- `plugins/tps25751-bench/skills/tps25751-pd-verify/SKILL.md`: the one source of the
  skill.
- `plugins/tps25751-bench/agents/pd-capture-analyst.md`: Claude Code only.
- `.agents/skills/tps25751-pd-verify/`: what Codex reads. A symlink to the plugin's
  skill directory if Codex follows symlinked skill directories (checked in this phase);
  otherwise a copy written by `tools/pd-bench/sync_skill.py`, whose `--check` mode
  `verify.py` runs beside the fixture check. The same script writes the copy into a
  downstream Codex project (`--dest <project>`), from the library tag that project pins.

The skill names tools as server plus tool (`km003c` `pd_capture_start`), which reads
correctly in both agents, and never assumes the analyst agent exists.

**Skill `tps25751-pd-verify`**

Arguments: `stimulus` (`gsrc` default when the board is sink, `ssrc` default when it is
source, `aneg`, `port_reset`, `restart`, `restart_cold`, `human`), `stop` (`contract`
default, `identity`, or a message count), and `save_fixture`. Preflight reads the power
role from `bench_hello` and refuses a stimulus that does not fit it.

1. **Preflight.** `device_info` (meter present, not busy, firmware), `bench_hello`
   (console present, protocol version matches, MODE is APP), one `measure` and one
   `bench_snapshot`; meter VBUS and 0x6A VBUS must agree within the ADC's 98 mV step
   plus meter tolerance.
2. **Capture first.** `pd_capture_start` with the chosen stop condition and a timeout.
   A capture records only what happens after monitoring is enabled, so the stimulus
   always comes after the start returns.
3. **Stimulus.** `pd_capture_mark`, then `bench_renegotiate`. For `human`, give the
   cold-attach steps from Phase 1 word for word and wait for the reply, since only a
   cold attach reliably starts a negotiation from the charger side.
4. **Wait.** `pd_capture_wait`, then `pd_capture_read(mode="summary")`.
5. **Sanity rules before any comparison:**
   - No Source_Capabilities and no Request, VBUS flat across the window: no negotiation
     happened. Report a stimulus or operator problem, not a protocol bug.
   - Only `conn` records: PD monitoring was not active. Report the header's
     `pd_monitor` field.
   - A Request with no Source_Capabilities before it: the capture started late.
6. **Snapshot.** `bench_snapshot` once PS_RDY has passed (plus `settle_ms` when
   identity is wanted).
7. **Compare**, from `pd_capture_read(mode="facts")` and the snapshot:
   - Source_Capabilities PDO count and words against 0x30 when the board is sink, or
     against 0x32 when the board is source and sent them.
   - The Request RDO against 0x35 bits 31:0; its object position selects the wire PDO
     that 0x34 bits 31:0 must equal; 0x34 bits 41:32 against bits 29:20 of PDO 1.
   - A Discover Identity ACK on SOP sent by the partner against 0x48 (response type,
     VDO count, VDOs). The board's own ACK and SOP' cable ACKs are reported but have no
     register to compare in this library.
   - Each word decoded three ways: the server's decoder, the library's (`bench_read`
     with `decode=True`), and the `usb-pd-docs` tables. A disagreement names the word,
     the field, the three values, and the raw hex.
8. **Report.** One table, one row per compared word, raw hex in every row, and a verdict.
9. **Fixture (optional).** Write the bundle of Phase 5 and run the generator.

**Large captures.** The main session reads only `summary` and `facts`, which are
bounded. A deeper look is needed when the capture has more than 200 messages, when a
sanity rule fires and the cause isn't obvious from the summary, or when the user asks
for a message-by-message review.

- Claude Code: the skill hands the capture path to the `pd-capture-analyst` agent. The
  agent's tools are the `km003c` read tools, `usb-pd-docs`, and Read; it returns a table
  of at most 30 rows, with hex.
- Codex: the skill pages through the file itself with `pd_capture_read(mode="messages",
  limit=40, since=<last t>)`, filtered by `sop` and `types` to the window around the
  problem, keeps only the rows that bear on it, and stops after five pages, reporting
  what it has not read.

**Source-role session.** With the board as source: board into the meter's receptacle
(input), meter's plug into an iPhone. Stimulus `ssrc`, `stop=identity`. This yields the
board's Source_Capabilities against 0x32 and the iPhone's Discover Identity ACK against
0x48, and becomes the 0x48 fixture bundle of Phase 5.

**Verification:** run the skill end to end with each stimulus that Phase 6 showed works,
once under Claude Code and once under Codex; deliberately break one decode in a scratch
build (for example the 0x34 bit 41:32 mapping) and confirm the skill reports it under
both; run the source-role session with the iPhone.

**Needs from you:** an iPhone and the source-role cabling for one session.

### Phase 8: Distribution to Claude Code and Codex

**Options weighed**

| Option | Works with this `lib/` layout | Works elsewhere | Codex | Notes |
|---|---|---|---|---|
| Relative `uv --directory ../km003c-mcp` | Yes | Only with the same sibling layout | Yes | What the docs servers do today. The working directory becomes the server repo, so relative capture paths land there. Nothing pins a version. |
| `uvx` from a git tag | Yes | Yes | Yes | `uvx --from git+https://github.com/forrestrae/mcp-powerz-km003c@v0.2.0 km003c-mcp`. Pinned, cached after the first run, starts in the project directory. Needs network once per new tag. |
| Claude Code plugin | Yes | Yes | No | Bundles the skill and agent; enabled per project through `enabledPlugins` and `extraKnownMarketplaces` in `.claude/settings.json`. |

**Recommendation: `uvx` from pinned git tags is how both agents get the servers; the
plugin is how Claude Code gets the skill and the analyst agent; `.agents/skills/` is
how Codex gets the skill.**

The same two `uvx` commands appear in both agents' configuration, in this library and in
each consuming project:

- `km003c`: `uvx --from git+https://github.com/forrestrae/mcp-powerz-km003c@vX.Y.Z km003c-mcp`,
  with `KM003C_CAPTURE_DIR=captures` and no `KM003C_ENABLE_TRIGGER`.
- `tps25751-bench`: `uvx --from "git+https://github.com/forrestrae/TPS25751@vX.Y.Z#subdirectory=tools/pd-bench" tps25751-pd-bench`.
  The library's CI already tags every release, and pinning the same tag as the library
  version a project depends on keeps the bench server matched to the console it
  compiled in.

**Claude Code**

- `.mcp.json` lists both servers; `.claude/settings.json` adds them to
  `enabledMcpjsonServers` and carries the permission rules of Phase 9.
- The plugin's marketplace is this library repository (`.claude-plugin/marketplace.json`),
  so a consuming project opts in with two settings entries. The plugin carries only the
  skill and the agent, not server definitions, so nothing is defined twice.
- Working on the server itself: a local-scope entry with the same name
  (`claude mcp add km003c --scope local -- uv --directory ../km003c-mcp run km003c-mcp`),
  which I expect takes precedence over the project entry; this phase confirms that.

**Codex**

`.codex/config.toml` in the project, which Codex reads because the project is trusted.
For this library:

```toml
[mcp_servers.tps25751-docs]
command = "uv"
args = ["--directory", "../device-register-mcp-server", "run", "server.py"]
env = { DEVICE_CONFIG = "devices/tps25751/device.json" }

[mcp_servers.usb-pd-docs]
command = "uv"
args = ["--directory", "../device-register-mcp-server", "run", "server.py"]
env = { DEVICE_CONFIG = "specifications/USB_PD/specification.json" }

[mcp_servers.km003c]
command = "uvx"
args = ["--from", "git+https://github.com/forrestrae/mcp-powerz-km003c@vX.Y.Z", "km003c-mcp"]
env = { KM003C_CAPTURE_DIR = "captures" }

[mcp_servers.tps25751-bench]
command = "uvx"
args = ["--from", "git+https://github.com/forrestrae/TPS25751@vX.Y.Z#subdirectory=tools/pd-bench", "tps25751-pd-bench"]

[mcp_servers.tps25751-bench.tools.bench_task]
approval_mode = "prompt"

[mcp_servers.tps25751-bench.tools.bench_write]
approval_mode = "prompt"

[mcp_servers.tps25751-bench.tools.bench_renegotiate]
approval_mode = "prompt"
```

- The relative `--directory` in the docs entries depends on Codex starting stdio servers
  in the project directory; this phase checks it, and the fallback is an absolute
  path in `~/.codex/config.toml`.
- If Codex does not load MCP servers from the project file, all four entries go in
  `~/.codex/config.toml` instead, where they apply to every project; the `uvx` pins then
  must match the library version of whichever project is open, which is the main
  cost of that fallback.
- Working on the server itself under Codex: start Codex with
  `-c 'mcp_servers.km003c.command="uv"' -c 'mcp_servers.km003c.args=["--directory","../km003c-mcp","run","km003c-mcp"]'`.
- The skill comes from `.agents/skills/tps25751-pd-verify/` (Phase 7).

**Files:** this library's `.mcp.json`, `.claude/settings.json`, `.codex/config.toml`
(new), `AGENTS.md`'s MCP section and per-tool table, `.claude-plugin/marketplace.json`,
`plugins/tps25751-bench/`, `.agents/skills/`.

**Verification (manual):**

- In this library: Claude Code's `/mcp` and Codex's MCP list both show the four servers;
  a `measure` call works from each; a Codex session shows the skill.
- A scratch PlatformIO project outside `lib/`, with only the configuration files, the
  plugin enabled, and the skill copied by `sync_skill.py --dest`, runs the skill end to
  end under each agent.

**Needs from you:** `.claude/settings.json` and `.codex/` are human-maintained and the
agent guard denies writes to them. I prepare the exact changes as a patch in the
branch's description; you apply them, or start a session with `WORKFLOW_GUARD_OFF=1`.
You also push the fork's tag.

### Phase 9: Trigger tools, cabling, and permissions

The code change is in Phase 3 (item 7). This phase documents the cabling and ships the
permission rules.

**How trigger mode works.** The meter becomes a PD sink toward the device on its
receptacle (input), negotiates what the tool asks for, and passes the resulting VBUS
through to its plug (output). Whatever is on the plug receives that voltage without
having negotiated it.

**Safe cabling**

- Charger on the receptacle, nothing on the plug: characterising a charger.
- TPS25751 board as **Source** on the receptacle, nothing on the plug: the meter
  requests the board's PDOs, including the SPR AVS APDO in PDO 5, which tests 0x32
  and 0x7A against a real sink. The board runs from its battery throughout. In scope,
  since the board can source; the bench console reads 0x32 and 0x7A after each request.

**Unsafe cabling**

- TPS25751 board as Sink on the plug with a charger on the receptacle. The trigger
  raises VBUS past anything in 0x33 or 0x37, and the board's sink path may already be
  closed from the 5 V implicit contract. The Apple 140 W charger is EPR-capable, so a
  request can reach 28 V.
- Any load on the plug that cannot take the highest voltage the input source offers.
  This includes the iPhone of the Phase 7 source-role session: trigger tools stay off
  whenever anything is on the plug.
- Running trigger tools while a TPS25751 verification capture is in progress: the meter
  then takes part in the negotiation it is supposed to observe.

**Claude Code permission rules** for `.claude/settings.json`. The `allow` list and the
bench entries apply in every project that uses the servers; the trigger entries matter
only in a project that sets `KM003C_ENABLE_TRIGGER=1`. One `ask` entry per tool, so a
server-wide allow cannot cover them:

```json
"permissions": {
  "allow": [
    "mcp__km003c__device_info", "mcp__km003c__measure", "mcp__km003c__pd_status",
    "mcp__km003c__pd_capture_start", "mcp__km003c__pd_capture_wait",
    "mcp__km003c__pd_capture_mark", "mcp__km003c__pd_capture_stop",
    "mcp__km003c__pd_capture_read", "mcp__km003c__device_release",
    "mcp__km003c__stream_start", "mcp__km003c__stream_status",
    "mcp__km003c__stream_samples", "mcp__km003c__stream_stop",
    "mcp__tps25751-bench__bench_hello", "mcp__tps25751-bench__bench_snapshot",
    "mcp__tps25751-bench__bench_read"
  ],
  "ask": [
    "mcp__km003c__trigger_detect", "mcp__km003c__trigger_enter",
    "mcp__km003c__trigger_pd_source_caps", "mcp__km003c__trigger_pd_request",
    "mcp__km003c__trigger_fast_charge", "mcp__km003c__trigger_reset",
    "mcp__km003c__trigger_close", "mcp__km003c__trigger_raw",
    "mcp__tps25751-bench__bench_task", "mcp__tps25751-bench__bench_write",
    "mcp__tps25751-bench__bench_renegotiate"
  ]
}
```

The bench's task, write, and renegotiate tools are on the `ask` list too, because they
can drop VBUS or reset the controller. `ask` rules do not protect a session running with
permissions bypassed; the environment flag does, which is why it stays the primary
guard.

**Codex.** Codex can require approval per MCP tool, but not reliably enough to guard the
triggers (see "Codex 0.171" above), so:

- The environment flag is the trigger guard under Codex. A Codex configuration never
  sets `KM003C_ENABLE_TRIGGER=1` in a checked-in `.codex/config.toml`; a trigger session
  sets it for one run with
  `-c 'mcp_servers.km003c.env.KM003C_ENABLE_TRIGGER="1"'` on the command line.
- As a second layer, that same command line sets `approval_mode = "prompt"` for each
  trigger tool, and the checked-in file sets it for `bench_task`, `bench_write`, and
  `bench_renegotiate` (Phase 8).
- With `approvals_reviewer = "auto_review"` in your user configuration, those prompts may
  go to the automatic reviewer rather than to you. This phase tests what happens
  (open question 1).

**Verification:** with the flag off, the trigger tools are absent from the tool list in
both agents. With it on, each call prompts in Claude Code. In Codex, record for one
trigger tool and for `bench_renegotiate` who answers the prompt: you, the automatic
reviewer, or nobody.

**Needs from you:** apply the settings changes (same constraint as Phase 8).

### Phase 10: Upstream contributions

After Phases 3 and 4 have been in use here, I list the `upstream/` branches with a short
description of each and draft the issue or pull request text. Nothing is opened until
you approve each one. Fork-only for good: the workflow tooling, the defaults that differ
from upstream's (idle release, trigger gate off), and anything mentioning the TPS25751.

## What each phase needs from you

| Phase | You |
|---|---|
| 0 | Approve this plan; trust `lib/TPS25751-km003c` in Codex and approve its hooks if you use Codex there; review two small merges |
| 1 | Three or four cold attaches, about ten minutes |
| 2 | Run the hook setup in the new clone (I run `gh repo fork`) |
| 3 to 7 | Review and push merges; one cold attach in Phase 4; the board on battery and an EEPROM-less board in Phase 6; an iPhone in Phase 7 |
| 5 | Start a `WORKFLOW_GUARD_OFF=1` session for the `verify.py` change, or make it yourself |
| 8, 9 | Apply the `.claude/settings.json` and `.codex/config.toml` changes; push the fork's tag |
| 10 | Approve each upstream issue or pull request |

## Decisions recorded

Your answers of 2026-11-22 to the first draft's open questions, and where each landed.

| # | Question | Answer | Effect |
|---|---|---|---|
| 1 | Bench power | Battery | Console survives VBUS drops; all stimuli offered on the bench (Phase 6 confirms). |
| 2 | Board as Source | Yes | `ssrc`, the source-role session, and the safe trigger cabling are in scope (Phases 6, 7, 9). |
| 3 | EEPROM on every board | No; patch loading exists | Console reloads the patch after `GAID`; `restart_cold` method (Phase 6). |
| 4 | Fork | `gh repo fork`, public, yours | `forrestrae/mcp-powerz-km003c` (Phase 2). |
| 5 | Bench in library; verify.py | Yes to both; guard-off session | `build/verify-pd-bench-checks` branch (Phase 5). |
| 6 | Dual USB serial | Yes | As planned (Phase 6). |
| 7 | Agents | Claude Code and Codex only | Phase 0 drops Cursor and VS Code; Phase 8 adds `.codex/config.toml`; skill mirrored to `.agents/skills/`. |
| 8 | Partner for 0x48 | iPhone, no configuration change | Source-role session and 0x48 bundle (Phases 5, 7). |
| 9 | Raw captures | `captures/` | Gitignored in each project (Phase 4). |
| 10 | Idle release | 2 s | Default `KM003C_IDLE_RELEASE_S=2` (Phase 3). |

## Open questions

Fewer, and none blocks Phase 0 or Phase 1.

1. **Codex approvals in this project.** Your user configuration sets
   `approvals_reviewer = "auto_review"`. Should this project's `.codex/config.toml`
   route approvals to you instead, if Codex allows that per project (Phase 9 checks),
   or do you accept the automatic reviewer and rely on the flag alone?
2. **The skill in downstream Codex projects.** I recommend copying it with
   `sync_skill.py --dest` from the library tag the project pins, so the skill matches the
   bench server. The alternative is a symlink into a sibling checkout, which only works
   with the `lib/` layout.
3. **The Codex row in `AGENTS.md` before `.codex/config.toml` exists.** Phase 0 can add
   the row marked "planned", or leave it out until you apply the file in Phase 8.

## Housekeeping

This file and `km003c-orchestrator-kickoff.md` are untracked in `docs/plans/` of the
main checkout `lib/TPS25751`, which is on `main`. Before implementation starts, you move
both into the `lib/TPS25751-km003c` worktree, and Phase 0 commits them there on
`docs/km003c-toolset-plan`. The main checkout then holds no copy.

Also untracked in the main checkout, and not part of this plan:
`docs/plans/feature/eeprom-update-planning-prompt.md`. It stays where it is. It does not
affect the worktree, since untracked files are not shared between worktrees.

## Phase 1 results

Run on 2026-11-22 from a scratch clone of `fejesd/mcp-powerz-km003c` at `4b26a6c`, with
`uv` choosing Python 3.14.2 on arm64. Meter serial `053893`: firmware 2.0.0 (dated
2025.2.20), hardware 2.3. Charger: Apple 140 W; sink: the TPS25751 board on battery.
Three cold attaches (S2 to S4). The scratch clone, `spike.py`, and the raw captures
were not kept.

### Decisions fixed

| Plan item | Decision | Evidence |
|---|---|---|
| Phase 3 item 1, backend | Order as planned: `KM003C_LIBUSB` (alias `KM003C_LIBUSB_DLL`), then `libusb_package`, then the system library. On this Mac `libusb_package` is enough. | S0: `libusb_package` loads its arm64 libusb and finds the meter; no override needed. |
| Phase 3 item 2, enable | Firmware 2.0.0 captures without EnablePdMonitor. Keep the planned fallback: enable at `start_pd`, treat Reject as non-fatal and log it, disable at `stop_pd`. A Reject also means "already enabled". | S2 captured all 16 messages after an accepted DisablePdMonitor. S1: a second enable returns Reject (`06`). |
| Phase 3 item 2, persistence | Enable on every `start_pd`; the monitor does not survive a release. | S1: after Disconnect, release, reclaim, and Connect, enable returns Accept again. |
| Phase 3 item 3, poll interval | Default 50 ms (the fork's current value), overridable with `KM003C_PD_POLL_MS`. 15 ms is not needed. | S4 at 1000 ms lost nothing: one poll returned the whole 16-message burst in a 142-byte payload. |
| Phase 3 item 4, release | Match the Disconnect reply by packet type, not by TID. | S1: the meter answers Disconnect with `03 11 00 00`, TID fixed at 0x11, so the fork's TID match waits out its 2 s timeout on every release (10 cycles: 2024 to 2250 ms; claim plus Connect 3 to 17 ms). |
| Phase 3 item 5, busy error | Map a claim failure with errno 19 on an enumerated meter to `DeviceBusyError`, and name the holder from `ioreg -l -w0`, property `UsbExclusiveOwner`, which reads for example `"pid 46945, python3.14"`. | S1: the second claim raised `USBError(19, 'No such device (it may have been disconnected)')`, which misstates the cause. |
| Phase 4, connection events | Keep the raw code in `conn` records; the fork's `parse_pd` discards it. Expect a bounce on attach. | S2 to S4: codes are the legacy 0x11 and 0x12 on firmware 2.0.0. S2 and S3 each showed connect, disconnect, connect within about 310 ms; S4 showed one connect. |
| Phase 6, port discovery | Plan on `TPS25751_BENCH_PORT` or the serial number; do not rely on an interface number. | S0: `pyserial` reports `interface=None` on macOS for both the meter and the Teensy. |

### Added to Phase 3: recovery from a lagging meter

On the first hardware run of the session, every reply arrived one request late: each
read returned the reply to the previous request, a drain read returned nothing, and the
fork's hardware tests failed 4 of 4 with "cannot connect: no reply". Clearing the halt on
both endpoints did not help; a USB device reset (`dev.reset()` in pyusb) did, and the
fault did not recur in the rest of the session. The cause is unknown. Phase 3 should
detect it (Connect times out while a reply with another TID arrives), reset once, and
retry, with a unit test on the fake transport. This is a small addition to item 4, not a
design change.

### Reference capture

S2, S3, and S4 recorded the same sequence, with the same wire bytes and MessageIDs, no
MessageID gaps per SOP and sender, and a GoodCRC after every message:

| t (ms, S3) | SOP | Message | Wire bytes |
|---|---|---|---|
| 14637552 | SOP' | Discover Identity REQ, source to cable | `8f10 01a000ff` |
| 14637555 | SOP' | Discover Identity ACK from the cable | `4f51 418000ff c220001c ba000000 10030500 52200821` |
| 14637557 | SOP | Source_Capabilities | `a141 2c918109 2cd10200 2cb10400 d6410600` |
| 14637562 | SOP | Request, RDO 0x4184B12C | `8210 2cb18441` |
| 14637564 | SOP | Accept | `a303` |
| 14637780 | SOP | PS_RDY, 216 ms after Accept | `a605` |
| 14640805 | SOP | Discover Identity REQ from the charger | `af17 01a000ff` |
| 14640811 | SOP | Discover Identity ACK from the board, VID 0x0451 | `8f42 41a000ff 51044000 00000000 11090000` |

The SOP messages match the table in the notes byte for byte. S3 sustained 54 polls per
second at 15 ms and S2 18 per second at 50 ms. The charger sends its own Discover
Identity 3.0 s after PS_RDY, which supports the planned `settle_ms` for `stop_on=identity`.
