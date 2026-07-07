# TPS25751 Register Implementation Plan v0.5

* **Date:** 2025-10-20
* **Last Updated:** 2026-07-06
* **Project:** TPS25751 USB-C PD Controller Library
* **Status:** 17/27 registers complete — Batch 0 (test harness) then Batches A–F (10 registers)

---

## Executive Summary

This document plans the implementation of the remaining registers of the TPS25751
USB-C Power Delivery controller. The remaining work starts with a **required
test-harness batch (Batch 0)**, then six register batches (A–F) ordered to
validate a **delegated implementation workflow** (cheap subagent implements,
expensive model reviews — see
[Delegated Implementation Workflow](#delegated-implementation-workflow)) on the
lowest-risk registers first, then ascend in complexity toward the PDO-encoding
registers. To start execution, see [Launching the Plan](#launching-the-plan).

**Total Registers:** 27 *(was 26 — ADC_RESULTS 0x6A added to scope in v0.4)*
**Currently Implemented:** 17
**Remaining to Implement:** 10
**Remaining Batches:** 7 (0 = test harness, A–F = registers)

**IMPORTANT:** This is a planning document. For implementation details, refer to:
- **[Implementation Standards](../engineering/STANDARDS.md)** - Class structure, validation, factory integration
- **[Technical Constraints](../engineering/CONSTRAINTS.md)** - Platform limitations, I2C protocol, USB PD complexity
- **[Testing Requirements](../engineering/TESTING.md)** - Unit, integration, and hardware testing
- **[Documentation Standards](../engineering/DOCUMENTATION.md)** - Doxygen, examples, troubleshooting
- **[Architecture](../engineering/ARCHITECTURE.md)** - System design and component relationships
- **[Code Review Guidelines](../engineering/CODE_REVIEW_GUIDELINES.md)** - The review-gate checklist used by the delegated workflow

---

## Completed Work (history)

### Phase 1 — Critical PD Diagnostics ✅ (2025-10-22)

All 6 registers implemented: GPIO_STATUS (0x72), PORT_CONTROL (0x29),
ACTIVE_PDO_CONTRACT (0x34), ACTIVE_RDO_CONTRACT (0x35),
RECEIVED_SOURCE_CAPABILITIES (0x30), RECEIVED_SINK_CAPABILITIES (0x31).
All 4 PDO types (Fixed, Battery, Variable, PPS/Augmented) with correct unit
conversions; full factory integration; three-tier validation; example updated.

### 4CC Command Interface ✅ (2026-06)

COMMAND (0x08, `TPS25751Command`) and DATA (0x09, `TPS25751Data`) implemented —
originally slated for old Phase 4, pulled forward to enable
`TPS25751::executeCommand()`, the `TPS25751DownstreamDevice` I2Cc proxy, and
`TPS25751PatchLoader` (PBM). See ADR-007/ADR-011 in
[ARCHITECTURE.md](../engineering/ARCHITECTURE.md).

---

## Current Implementation Status

### ✅ Implemented Registers (17)

| Register | Address | Class Name | Size | Purpose |
|----------|---------|------------|------|---------|
| MODE | 0x03 | `TPS25751Mode` | 4 | Operating mode and capabilities |
| COMMAND | 0x08 | `TPS25751Command` | 4 | 4CC task code, clear/rejected state |
| DATA | 0x09 | `TPS25751Data` | 64 | Generic 4CC task payload buffer |
| INT_EVENT1 | 0x14 | `TPS25751InterruptEvent` | 11 | Interrupt events |
| STATUS | 0x1A | `TPS25751Status` | 5 | Connection state, VBUS status, power role |
| POWER_PATH_STATUS | 0x26 | `TPS25751PowerPathStatus` | 5 | Power path and charging status |
| PORT_CONFIGURATION | 0x28 | `TPS25751PortConfig` | 17 | Port configuration settings |
| PORT_CONTROL | 0x29 | `TPS25751PortControl` | 4 | Port control settings, role swaps |
| BOOT_STATUS | 0x2D | `TPS25751BootFlags` | 5 | Boot and initialization flags |
| RECEIVED_SOURCE_CAPABILITIES | 0x30 | `TPS25751ReceivedSourceCaps` | 53 | Source capabilities (up to 7 PDOs) |
| RECEIVED_SINK_CAPABILITIES | 0x31 | `TPS25751ReceivedSinkCaps` | 53 | Sink capabilities (up to 7 PDOs) |
| ACTIVE_PDO_CONTRACT | 0x34 | `TPS25751ActivePDOContract` | 6 | Active USB PD power contract |
| ACTIVE_RDO_CONTRACT | 0x35 | `TPS25751ActiveRDOContract` | 16 | Active USB PD request contract |
| POWER_STATUS | 0x3F | `TPS25751PowerStatus` | 2 | Power status information |
| PD_STATUS | 0x40 | `TPS25751PDStatus` | 4 | Power Delivery status |
| TYPEC_STATE | 0x69 | `TPS25751TypeCState` | 4 | Type-C state machine status |
| GPIO_STATUS | 0x72 | `TPS25751GPIOStatus` | 8 | GPIO pin status and direction |

### ❌ Unimplemented Registers (10)

Sizes below come from the shipped `RegisterInfo` table in
`include/TPS25751RegisterAddress.h` (the single source of truth), **not** from
earlier revisions of this plan, which disagreed with it. Verify against the
`tps25751-docs` MCP server before implementing; where they conflict, trust
hardware behavior (cf. the ACTIVE_RDO_CONTRACT 16-byte note in the header).

| # | Register | Address | Size | Access | Batch | Planned Class |
|---|----------|---------|------|--------|-------|---------------|
| 1 | SLEEP_CONTROL | 0x70 | 1 | R/W | A | `TPS25751SleepControl` |
| 2 | CUSTOMER_USE | 0x06 | 8 | R/W | A | `TPS25751CustomerUse` |
| 3 | INT_MASK1 | 0x16 | 11 | R/W | B | `TPS25751InterruptMask` |
| 4 | INT_CLEAR1 | 0x18 | 11 | W | B | `TPS25751InterruptClear` |
| 5 | ADC_RESULTS | 0x6A | 13 | R | C | `TPS25751ADCResults` |
| 6 | TRANSMIT_SOURCE_CAPABILITIES | 0x32 | 63 | R/W | D | `TPS25751TransmitSourceCaps` |
| 7 | TRANSMIT_SINK_CAPABILITIES | 0x33 | 53 | R/W | D | `TPS25751TransmitSinkCaps` |
| 8 | AUTONEGOTIATE_SINK | 0x37 | 24 | R/W | E | `TPS25751AutoNegotiateSink` |
| 9 | IO_CONFIG | 0x5C | 49 | R/W | E | `TPS25751IOConfig` |
| 10 | LIQUID_DETECTION_CONFIG | 0x98 | 11 | R/W | F | `TPS25751LiquidDetectionConfig` |

All 10 addresses and sizes are **already present** in
`TPS25751Registers::Address` / `RegisterInfo`; per-register work starts at the
class + factory `case`, not the enum. `TPS25751::writeRegister()` already exists,
so R/W registers need encode logic and a write-path convenience method, not new
I2C plumbing.

---

## Implementation Order & Rationale

Batches are ordered so that the delegated workflow is proven on trivial registers
before it is trusted with the hard ones, and so that each batch is a coherent,
independently reviewable and publishable unit (one branch, one review, one merge).
Batch 0 comes first because every later batch's quality gate depends on it.

### Batch 0 — Test Harness (REQUIRED prerequisite; orchestrator-implemented, not delegated)

- **Why first:** `test/` is still a placeholder — the >90% coverage requirement
  in [TESTING.md](../engineering/TESTING.md) is currently aspirational, and
  earlier per-register "unit tests" line items were never runnable. The
  delegated workflow's review gate is markedly weaker if subagent-written tests
  cannot execute, so **no register batch starts until this one is done**.
- Stand up a minimal PlatformIO **`native` test environment** (Unity) that can
  instantiate register classes from canned byte buffers — no hardware, no
  `Wire`. Decode/encode logic is platform-independent and tests cleanly
  off-target.
- Seed it with tests for at least one *existing* register class (e.g.
  `TPS25751Status`) to prove the harness against known-good code, and wire the
  run command (`pio test -e native` or equivalent) into this plan's per-batch
  loop and kickoff prompts.
- Update [TESTING.md](../engineering/TESTING.md) to describe the real, working
  workflow (replacing the aspirational wording), and confirm `test/` stays in
  the `export.exclude` list in `library.json` so it never ships in the registry
  tarball.
- **Not delegated:** this is infrastructure/architecture work, which the
  workflow section explicitly reserves for the orchestrator.

### Batch A — Pilot: Trivial R/W Registers (SLEEP_CONTROL, CUSTOMER_USE)

- **Why first register batch:** Smallest possible surface (1 and 8 bytes, no sub-structure to
  speak of). Their purpose here is to *validate the delegated
  implement-then-review pipeline end-to-end* — work-order format, subagent
  conventions, review gate, doc updates — at minimal cost. If the pipeline
  produces bad output here, fix the pipeline before Batch B.
- SLEEP_CONTROL: parse sleep-mode enable/config bits; read + write path.
- CUSTOMER_USE: generic 8-byte application-defined blob; hex accessor +
  raw read/write; interpretation left to the application.

### Batch B — Interrupt Control (INT_MASK1, INT_CLEAR1)

- **Why second:** Highest-value remaining pair; both mirror the existing
  `TPS25751InterruptEvent` bit layout, so the reference implementation is
  in-repo and the subagent's job is mostly a disciplined transcription.
  Together with INT_EVENT1 they complete the interrupt-driven story.
- INT_MASK1: same bitfield map as INT_EVENT1; read + write; per-source
  enable/disable helpers.
- INT_CLEAR1: write-only; `clear(bit)` / `clearAll()` helpers; document
  write-only semantics in basic validation (reads are not meaningful).
- **Follow-up (optional, orchestrator task):** `TPS25751PatchLoader` steps 3/11
  currently touch INT_MASK1/INT_CLEAR1 via raw writes; consider refactoring to
  the new classes only if it doesn't complicate the PBM flow.

### Batch C — Telemetry (ADC_RESULTS)

- **Why third:** Read-only decode, immediately useful for hardware debugging
  (VBUS/VIN/temperature measurements), and the newest addition to scope — it was
  missing from earlier plan revisions despite being in the address enum.
- Decode the 13-byte measurement block into named, unit-converted accessors;
  note in the class docs that the controller does **not** update this register
  in Unconnected-Sleep mode (stale reads are not an error).

### Batch D — Capability Encoding (TRANSMIT_SOURCE_CAPS, TRANSMIT_SINK_CAPS)

- **Why fourth (and not delegated blindly):** This is the hardest remaining
  work — it introduces the library's first **PDO encode** path (Received*Caps
  only decode). PDO unit conversion (esp. PPS/APDO scaling) is the #1 bug
  pattern in [CODE_REVIEW_GUIDELINES.md](../engineering/CODE_REVIEW_GUIDELINES.md).
  See the workflow section for the tightened delegation rules for this batch.
- Reuse/extract the PDO decode helpers from `TPS25751ReceivedSourceCaps` /
  `ReceivedSinkCaps` rather than duplicating them; add the encode inverse and
  round-trip (encode→decode) unit checks.
- Validate PDO count ≤ 7 and per-type field ranges in data validation; flag
  semantically inconsistent capability sets (e.g. non-Fixed PDO first) in
  semantic validation.
- Note the asymmetric sizes: TRANSMIT_SOURCE is 63 bytes, TRANSMIT_SINK is 53.

### Batch E — Negotiation & I/O Configuration (AUTONEGOTIATE_SINK, IO_CONFIG)

- AUTONEGOTIATE_SINK (24 bytes): auto-negotiation preferences — voltage/current
  ranges and priorities; R/W with range validation.
- IO_CONFIG (49 bytes): per-pin GPIO configuration; R/W. Large but repetitive
  (per-pin sub-records) — good delegation target once Batch A–B conventions
  are proven.

### Batch F — Niche (LIQUID_DETECTION_CONFIG)

- Liquid/moisture detection thresholds and response actions; R/W. Last because
  it is a specialized safety feature with no current example coverage.

---

## Delegated Implementation Workflow

**Decision: yes — delegate implementation to a less expensive subagent, gate with
a more expensive model running `/code-review` at high effort.** This work is a
good fit for that split, for reasons specific to this codebase:

- The per-register work is **highly formulaic**: STANDARDS.md prescribes the
  exact class structure, 17 in-repo reference implementations exist to
  pattern-match, and the `tps25751-docs` MCP server supplies ground-truth
  bit layouts. Boilerplate-following is what cheaper models do well and where
  most of the token spend goes.
- The failure modes are **known and enumerable**: the platform traps (no RTTI,
  `F()` macro, I2C length byte, bitfield shift bugs, PDO unit conversion) are
  already cataloged in CONSTRAINTS.md and CODE_REVIEW_GUIDELINES.md — exactly
  the kind of checklist a high-effort review by a stronger model verifies
  reliably.
- Batches are **small and independently verifiable** (compile gate via
  `pio run`), so a bad delegated attempt is cheap to detect and redo.

### Roles

| Role | Who | Responsibility |
|------|-----|----------------|
| Orchestrator / quality gate | Expensive model (the main Claude Code session — Fable/Opus class) | Writes the work order, spawns the subagent, runs `/code-review high` on the result, applies/adjudicates findings, verifies build, updates docs, merges |
| Implementer | Cheap subagent (`general-purpose` agent, **Sonnet**; **Haiku** acceptable for Batch A only) | Implements the register class(es), factory cases, convenience methods, Doxygen, and tests per the work order |

### Per-Batch Loop

1. **Work order (orchestrator).** Before spawning, the orchestrator assembles a
   self-contained prompt containing:
   - Register facts fetched from the `tps25751-docs` MCP server
     (`get_register` / `explain_bitfield`) **pasted into the prompt** — do not
     assume the subagent will query MCP correctly on its own;
   - The file list to create/modify (header in `include/`, impl in `src/`,
     factory `case`s in `TPS25751RegisterFactory`, convenience method(s) in
     `TPS25751.h/.cpp`);
   - Pointers to the closest existing reference class (e.g. "mirror
     `TPS25751InterruptEvent` for INT_MASK1") and to STANDARDS.md /
     CONSTRAINTS.md as required reading;
   - The build command that must pass: `pio run -e example-read-registers`;
   - The Common Mistakes list from AGENTS.md, verbatim.
2. **Implementation (subagent).** Spawn with `isolation: "worktree"` so the
   subagent works on an isolated copy; it implements, builds, and reports.
3. **Quality gate (orchestrator).** On the returned diff:
   - Run **`/code-review` at high effort** — findings adjudicated against
     [CODE_REVIEW_GUIDELINES.md](../engineering/CODE_REVIEW_GUIDELINES.md);
   - Re-verify the build compiles **without warnings**;
   - Spot-check the three-tier validation and debug-print format against
     STANDARDS.md (formulaic sections a reviewer skims are where template
     drift hides);
   - Cross-check every bit offset/width against the MCP register data — this
     is the one check the review pass cannot do from the diff alone.
4. **Fix loop.** Small findings: orchestrator fixes directly. Structural
   findings: send back to the *same* subagent (SendMessage, context intact)
   with the findings list. **Escalation rule:** if a batch fails the review
   gate twice with correctness findings, the orchestrator takes over
   implementation for that batch — a third cheap attempt costs more than doing
   it once well.
5. **Docs & merge.** Orchestrator performs the
   [Documentation Update Checklist](#documentation-update-checklist-per-batch),
   commits, and merges. Remember: pushing to `main` auto-publishes to the
   PlatformIO Registry — since each batch adds public API, put **`#minor`** in
   the merge-commit subject line (see AGENTS.md "Publishing the Library").
6. **Hardware validation (human).** Where hardware is available, validate reads
   (and writes on R/W registers) against a live TPS25751 before considering the
   batch done; record anomalies (e.g. size mismatches vs TRM) as comments in
   `TPS25751RegisterAddress.h` and as CONSTRAINTS.md entries.

### Batch-D Tightening

For TRANSMIT_*_CAPABILITIES, delegation stays viable but the work order must be
stricter: the **orchestrator writes the full PDO bit-layout and unit-conversion
spec into the prompt** (all four PDO types, PPS scaling included) instead of
letting the subagent derive it; the subagent must include encode↔decode
round-trip tests; and the review gate must independently re-derive at least one
encoded PDO by hand. Use Sonnet, not Haiku. If the pilot batches surfaced any
workflow weakness, do Batch D orchestrator-implemented instead.

### When *Not* to Delegate

Do not delegate: factory/architecture refactors, CONSTRAINTS.md/ADR authoring,
anything requiring hardware interaction, or fix-ups after a failed review (see
escalation rule). Those stay with the orchestrator.

---

## Launching the Plan

Execution is session-driven: **run each batch as its own Claude Code session**,
started from this library root. A fresh session per batch keeps the orchestrator's
context small, and keeps the review gate from being biased by leftover
implementation context. One batch = one branch = one review = one merge.

### Pre-flight (once, before Batch 0)

1. `git status` clean on `main`; sibling repo `../device-register-mcp-server`
   checked out side-by-side (the `tps25751-docs` MCP server runs from it).
2. Confirm the MCP server responds — e.g. ask the session to fetch a known
   register via `mcp__tps25751-docs__get_register`.
3. Confirm the baseline builds: `pio run -e example-read-registers`.

### Kickoff prompt — Batch 0

Paste into a new session:

```text
Execute Batch 0 of docs/plans/plan-impl-v01.md: stand up the native unit-test
harness. Implement it yourself (do not delegate). Seed it with tests for one
existing register class, make the native test run pass, update
docs/engineering/TESTING.md per the plan's documentation checklist, and commit
on branch feat/registers-batch-0. Stop before merging to main.
```

### Kickoff prompt — Batches A–F (template)

Paste into a new session, substituting the batch letter:

```text
Execute Batch <X> of docs/plans/plan-impl-v01.md using its Delegated
Implementation Workflow: write the work order (fetch register data from the
tps25751-docs MCP server and paste it into the prompt), delegate implementation
to a general-purpose subagent (model: sonnet) in an isolated worktree, then act
as the quality gate — run /code-review at high effort, adjudicate findings per
docs/engineering/CODE_REVIEW_GUIDELINES.md, verify a warning-free
`pio run -e example-read-registers` build and a green native test run, do the
manual bit-offset cross-check against the MCP data, and complete the plan's
Documentation Update Checklist. Work on branch feat/registers-batch-<x>.
Stop before merging to main, and end with a summary of the review findings and
how each was resolved.
```

For Batch A only, append: `This is the pilot batch — also report any weaknesses
found in the delegated workflow itself, and propose plan amendments.` For
Batch D, remind the session of the plan's Batch-D Tightening section.

### Merge & release (you, per batch)

Merging to `main` **auto-publishes to the PlatformIO Registry**, which is why
every kickoff prompt ends with "stop before merging" — you approve each release:

1. Review the session's findings summary and the diff.
2. Merge with **`#minor`** as a whitespace-delimited token in the merge-commit
   subject line (each register batch adds public API). Batch 0 adds no public
   API — a plain (patch) merge subject is fine there.
3. Hardware-validate per step 6 of the per-batch loop when hardware is on the
   bench; file anomalies back into CONSTRAINTS.md via a follow-up session.

### Optional deeper gate

For Batch D (or any batch you're uneasy about), you can additionally run
`/code-review ultra <PR#>` (or the no-arg branch form) yourself before merging —
it is user-triggered and billed, and the orchestrator cannot launch it for you.

---

## Documentation Update Checklist (per batch)

Run this checklist at the end of **every** batch, before merge. The orchestrator
(not the subagent) owns it.

### Always

| File | What to update |
|------|----------------|
| `AGENTS.md` (repo root — **edit AGENTS.md, never CLAUDE.md**, which is a symlink to it) | "Implemented Registers" count and list in *Implementation Status*; the *Register Classes* list (add the new class + one-line description); "Last Updated" date; remove/adjust any statement made stale by the batch (e.g. "remaining N registers") |
| `docs/plans/plan-impl-v01.md` (this file) | Move the batch's registers from *Unimplemented* to *Implemented* with completion date; update counts in the Executive Summary; add a Revision History row |
| Doxygen in the new headers/sources | Per [DOCUMENTATION.md](../engineering/DOCUMENTATION.md) — done during implementation, verified at review |

### When applicable

| File | Trigger |
|------|---------|
| `examples/read-registers/src/main.cpp` | New **readable** registers should be demonstrated in the read-registers example (as prior phases did) |
| `examples/README.md` + `examples/AGENTS.md` | Any example added or whose demonstrated-register list materially changed |
| `README.md` (repo root) | Only if a batch adds a user-visible *capability* worth a Highlights bullet (e.g. Batch D: "advertised-capability configuration"). The README deliberately omits per-register detail — do not add a register table there |
| `docs/engineering/CONSTRAINTS.md` | Any newly discovered hardware trap: size mismatch vs TRM, write-only read behavior, update-frequency caveats (ADC_RESULTS in sleep), encode quirks |
| `docs/engineering/CODE_REVIEW_GUIDELINES.md` | Any *new bug pattern* the review gate actually caught — feed the gate's findings back into the checklist |
| `docs/engineering/ARCHITECTURE.md` | Only for genuinely new decisions (e.g. if Batch D introduces a shared PDO encode/decode utility, record it as a new ADR); not for routine register additions |
| `docs/engineering/TESTING.md` | When Batch 0 lands the harness: replace the aspirational wording with the real `pio test` workflow |
| `../BQ25798/AGENTS.md`, `../device-register-mcp-server/*` (sibling repos) | Normally **no change** — nothing in Batches A–F touches the downstream-device contract or the MCP server. Flag it explicitly if a batch somehow does |

### Never

- `library.json` / `library.properties` **version fields** — the CI bump script
  (`scripts/bump_version.py`) is the single source of truth. Version intent is
  expressed only via `#minor`/`#major` tokens in the commit subject line.

---

## Estimated Effort

Historical calibration: Phase 1 (6 registers, incl. the hardest PDO decode work)
took ~12 hours orchestrator-implemented. Delegation trades wall-clock elapsed
time for lower cost; orchestrator attention per batch is the scarce resource.

| Batch | Registers | Complexity | Est. orchestrator attention | Est. subagent scope |
|-------|-----------|------------|-----------------------------|---------------------|
| 0 (required first) | test harness | Medium | 2–3 h (not delegated) | — |
| A | SLEEP_CONTROL, CUSTOMER_USE | Trivial | 1 h (mostly pipeline setup) | Small |
| B | INT_MASK1, INT_CLEAR1 | Low (mirror INT_EVENT1) | 1–2 h | Medium |
| C | ADC_RESULTS | Low | 1 h | Small |
| D | TRANSMIT_SOURCE/SINK_CAPS | **High** (PDO encode) | 3–4 h (spec-writing + deep review) | Large |
| E | AUTONEGOTIATE_SINK, IO_CONFIG | Medium | 2 h | Large |
| F | LIQUID_DETECTION_CONFIG | Low | 1 h | Small |

---

## Success Criteria

### Functional
- All 27 registers implemented with full parsing (and encoding where R/W)
- Factory `case` present for every address in every `createRegister()` switch
- Three-tier validation (basic, data, semantic) on every new class
- Debug printing per STANDARDS.md format
- `TPS25751` convenience read (and write, where applicable) methods added

### Quality
- Every batch passed the `/code-review high` gate with correctness findings resolved
- Clean compile, no warnings, on Teensy 4.x (`pio run -e example-read-registers`)
- Native test run green for every batch (harness from Batch 0 is a hard prerequisite)
- Hardware validation performed where hardware allows; anomalies documented

### Documentation
- Per-batch [Documentation Update Checklist](#documentation-update-checklist-per-batch) completed — AGENTS.md register list/count never stale after a merge
- Doxygen complete per DOCUMENTATION.md
- New traps fed back into CONSTRAINTS.md / CODE_REVIEW_GUIDELINES.md

---

## Dependencies and Risks

### Dependencies
- TI TPS25751 Technical Reference Manual + `tps25751-docs` MCP server (sibling `lib/device-register-mcp-server` repo)
- USB Power Delivery Specification 3.1 (Batch D)
- Teensy Arduino core, PlatformIO build system

### Risks
| Risk | Impact | Mitigation |
|------|--------|------------|
| Subagent template drift (subtly wrong validation/debug structure) | Medium | Review gate spot-checks against STANDARDS.md; pilot batch exists to catch this cheaply |
| PDO encode unit-conversion bugs (Batch D) | High | Orchestrator-authored bit-layout spec in the work order; round-trip tests; manual re-derivation at review |
| TRM sizes vs hardware behavior mismatch | Medium | Trust `RegisterInfo` + hardware; document mismatches in header comments and CONSTRAINTS.md (precedent: ACTIVE_RDO_CONTRACT 16B) |
| Write-path bricking/misconfiguring a live board (IO_CONFIG, PORT-adjacent writes) | Medium | Hardware validation reads back every write; keep writes out of examples' default loop |
| No runnable test harness | Medium | Batch 0 is a required prerequisite — register batches do not start until the native test run is green |
| Registry auto-publish on every merge to `main` | Low | Batch = merge = release is intentional; use `#minor` for API additions, verify tarball with `pio pkg pack` if export list changes |

---

## Future Enhancements

Unchanged from v0.3: caching layer, async I2C, register-change subscriptions,
configuration profiles, power-profile builder, interrupt-driven architecture,
data logging. Additional platform support (ESP32, RP2040) tracked in AGENTS.md.

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2025-10-20 | Claude Code | Initial planning document |
| 0.2 | 2025-10-20 | Claude Code | Removed duplicate content, added references to engineering docs |
| 0.3 | 2025-10-22 | Claude Code | Phase 1 complete: 15/26 implemented, 11 remaining |
| 0.4 | 2026-07-06 | Claude Code | Recorded COMMAND/DATA as complete (17 done); added ADC_RESULTS to scope (27 total, 10 remaining); corrected register sizes to match shipped `RegisterInfo` (TRANSMIT caps 63/53 not 29, IO_CONFIG 49 not 48, ACTIVE_RDO 16); replaced phase structure with Batches A–F; added Delegated Implementation Workflow (cheap subagent + `/code-review high` gate); added per-batch Documentation Update Checklist and test-harness prerequisite (Batch 0) |
| 0.5 | 2026-07-06 | Claude Code | Promoted Batch 0 (test harness) from optional to **required prerequisite** before Batch A; added "Launching the Plan" section with pre-flight checks, per-batch kickoff prompts, and merge/release steps |

---

## Appendix A: Register Quick Reference

| Reg# | Name | Addr | Size | R/W | Batch | Status |
|------|------|------|------|-----|-------|--------|
| 1 | MODE | 0x03 | 4 | R | — | ✅ Done |
| 2 | CUSTOMER_USE | 0x06 | 8 | R/W | A | ❌ |
| 3 | COMMAND | 0x08 | 4 | W | — | ✅ Done (2026-06) |
| 4 | DATA | 0x09 | 64 | R/W | — | ✅ Done (2026-06) |
| 5 | INT_EVENT1 | 0x14 | 11 | R | — | ✅ Done |
| 6 | INT_MASK1 | 0x16 | 11 | R/W | B | ❌ |
| 7 | INT_CLEAR1 | 0x18 | 11 | W | B | ❌ |
| 8 | STATUS | 0x1A | 5 | R | — | ✅ Done |
| 9 | POWER_PATH_STATUS | 0x26 | 5 | R | — | ✅ Done |
| 10 | PORT_CONFIGURATION | 0x28 | 17 | R | — | ✅ Done |
| 11 | PORT_CONTROL | 0x29 | 4 | R/W | — | ✅ Done (Phase 1) |
| 12 | BOOT_STATUS | 0x2D | 5 | R | — | ✅ Done |
| 13 | RECEIVED_SOURCE_CAPABILITIES | 0x30 | 53 | R | — | ✅ Done (Phase 1) |
| 14 | RECEIVED_SINK_CAPABILITIES | 0x31 | 53 | R | — | ✅ Done (Phase 1) |
| 15 | TRANSMIT_SOURCE_CAPABILITIES | 0x32 | 63 | R/W | D | ❌ |
| 16 | TRANSMIT_SINK_CAPABILITIES | 0x33 | 53 | R/W | D | ❌ |
| 17 | ACTIVE_PDO_CONTRACT | 0x34 | 6 | R | — | ✅ Done (Phase 1) |
| 18 | ACTIVE_RDO_CONTRACT | 0x35 | 16 | R | — | ✅ Done (Phase 1) |
| 19 | AUTONEGOTIATE_SINK | 0x37 | 24 | R/W | E | ❌ |
| 20 | POWER_STATUS | 0x3F | 2 | R | — | ✅ Done |
| 21 | PD_STATUS | 0x40 | 4 | R | — | ✅ Done |
| 22 | IO_CONFIG | 0x5C | 49 | R/W | E | ❌ |
| 23 | TYPEC_STATE | 0x69 | 4 | R | — | ✅ Done |
| 24 | ADC_RESULTS | 0x6A | 13 | R | C | ❌ |
| 25 | SLEEP_CONTROL | 0x70 | 1 | R/W | A | ❌ |
| 26 | GPIO_STATUS | 0x72 | 8 | R | — | ✅ Done (Phase 1) |
| 27 | LIQUID_DETECTION_CONFIG | 0x98 | 11 | R/W | F | ❌ |

**Note:** For USB PD PDO type details, see [CONSTRAINTS.md - USB Power Delivery Complexity](../engineering/CONSTRAINTS.md#usb-power-delivery-complexity)

---

*End of Implementation Plan v0.5*
