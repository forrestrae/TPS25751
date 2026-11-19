# TPS25751 Register Implementation Plan v1.2

* **Date:** 2025-10-20
* **Last Updated:** 2026-07-07
* **Project:** TPS25751 USB-C PD Controller Library
* **Status:** **27/27 registers complete** — Batch 0 (test harness) and Batch A (SLEEP_CONTROL, CUSTOMER_USE) **done 2026-07-06**, Batch B (INT_MASK1, INT_CLEAR1), Batch C (ADC_RESULTS), Batch D (TRANSMIT_SOURCE_CAPS, TRANSMIT_SINK_CAPS), and Batch E (AUTONEGOTIATE_SINK, IO_CONFIG) **done 2026-07-07**, Batch F (LIQUID_DETECTION_CONFIG) **done 2026-07-08** — the register set is now fully implemented

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
**Currently Implemented:** 27
**Remaining to Implement:** 0
**Remaining Batches:** 0 — all batches complete (Batch 0 test harness + Batch A pilot done 2026-07-06, Batch B + Batch C + Batch D + Batch E done 2026-07-07, Batch F done 2026-07-08)

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

### ✅ Implemented Registers (27)

| Register | Address | Class Name | Size | Purpose |
|----------|---------|------------|------|---------|
| MODE | 0x03 | `TPS25751Mode` | 4 | Operating mode and capabilities |
| CUSTOMER_USE | 0x06 | `TPS25751CustomerUse` | 8 | Opaque application-defined blob (R/W) |
| COMMAND | 0x08 | `TPS25751Command` | 4 | 4CC task code, clear/rejected state |
| DATA | 0x09 | `TPS25751Data` | 64 | Generic 4CC task payload buffer |
| INT_EVENT1 | 0x14 | `TPS25751InterruptEvent` | 11 | Interrupt events |
| INT_MASK1 | 0x16 | `TPS25751InterruptMask` | 11 | Interrupt enable mask (R/W) |
| INT_CLEAR1 | 0x18 | `TPS25751InterruptClear` | 11 | Interrupt event clear (write-only) |
| STATUS | 0x1A | `TPS25751Status` | 5 | Connection state, VBUS status, power role |
| POWER_PATH_STATUS | 0x26 | `TPS25751PowerPathStatus` | 5 | Power path and charging status |
| PORT_CONFIGURATION | 0x28 | `TPS25751PortConfig` | 17 | Port configuration settings |
| PORT_CONTROL | 0x29 | `TPS25751PortControl` | 4 | Port control settings, role swaps |
| BOOT_STATUS | 0x2D | `TPS25751BootFlags` | 5 | Boot and initialization flags |
| RECEIVED_SOURCE_CAPABILITIES | 0x30 | `TPS25751ReceivedSourceCaps` | 53 | Source capabilities (up to 7 PDOs) |
| RECEIVED_SINK_CAPABILITIES | 0x31 | `TPS25751ReceivedSinkCaps` | 53 | Sink capabilities (up to 7 PDOs) |
| TRANSMIT_SOURCE_CAPABILITIES | 0x32 | `TPS25751TransmitSourceCaps` | 63 | Advertised source capabilities, PDO encode (R/W) |
| TRANSMIT_SINK_CAPABILITIES | 0x33 | `TPS25751TransmitSinkCaps` | 53 | Advertised sink capabilities, PDO encode (R/W) |
| ACTIVE_PDO_CONTRACT | 0x34 | `TPS25751ActivePDOContract` | 6 | Active USB PD power contract |
| ACTIVE_RDO_CONTRACT | 0x35 | `TPS25751ActiveRDOContract` | 16 | Active USB PD request contract |
| AUTONEGOTIATE_SINK | 0x37 | `TPS25751AutoNegotiateSink` | 24 | Sink power-negotiation configuration (R/W) |
| POWER_STATUS | 0x3F | `TPS25751PowerStatus` | 2 | Power status information |
| PD_STATUS | 0x40 | `TPS25751PDStatus` | 4 | Power Delivery status |
| IO_CONFIG | 0x5C | `TPS25751IOConfig` | 49 | Boot-fixed GPIO configuration (R) |
| TYPEC_STATE | 0x69 | `TPS25751TypeCState` | 4 | Type-C state machine status |
| ADC_RESULTS | 0x6A | `TPS25751ADCResults` | 13 | Internal-ADC telemetry: per-pin voltage + PP_5V1 current (R) |
| SLEEP_CONTROL | 0x70 | `TPS25751SleepControl` | 1 | Sleep-mode enable + sleep-time (R/W) |
| GPIO_STATUS | 0x72 | `TPS25751GPIOStatus` | 8 | GPIO pin status and direction |
| LIQUID_DETECTION_CONFIG | 0x98 | `TPS25751LiquidDetectionConfig` | 11 | Liquid/moisture-detection config, corrosion mitigation (R/W) |

### ❌ Unimplemented Registers (0)

**All 27 registers are implemented.** LIQUID_DETECTION_CONFIG (0x98) was completed
in Batch F (2026-07-08); nothing remains.

*(SLEEP_CONTROL 0x70 and CUSTOMER_USE 0x06 moved to Implemented — Batch A, 2026-07-06;
INT_MASK1 0x16 and INT_CLEAR1 0x18 moved to Implemented — Batch B, 2026-07-07;
ADC_RESULTS 0x6A moved to Implemented — Batch C, 2026-07-07;
TRANSMIT_SOURCE_CAPABILITIES 0x32 and TRANSMIT_SINK_CAPABILITIES 0x33 moved to
Implemented — Batch D, 2026-07-07; AUTONEGOTIATE_SINK 0x37 and IO_CONFIG 0x5C moved
to Implemented — Batch E, 2026-07-07; LIQUID_DETECTION_CONFIG 0x98 moved to
Implemented — Batch F, 2026-07-08.)*

Every register address and size lived in `TPS25751Registers::Address` /
`RegisterInfo` from the outset; per-register work started at the class + factory
`case`, not the enum. `TPS25751::writeRegister()` and (since Batch B) the
base-class `setBits()` write primitive + typed `writeRegister<T>()` keyed on a
class `kAddress` served every R/W register through to the last (Batch F's
LIQUID_DETECTION_CONFIG), so no new I2C plumbing was needed after Batch B.

---

## Implementation Order & Rationale

Batches are ordered so that the delegated workflow is proven on trivial registers
before it is trusted with the hard ones, and so that each batch is a coherent,
independently reviewable and publishable unit (one branch, one review, one merge).
Batch 0 comes first because every later batch's quality gate depends on it.

### Batch 0 — Test Harness (REQUIRED prerequisite; orchestrator-implemented, not delegated) — ✅ DONE 2026-07-06

**Completed on branch `feat/registers-batch-0`:** `[env:native]` in
`platformio.ini` (Unity, host-run, Arduino API stubs in `test/support/native/`),
seed suite `test/test_native/test_TPS25751Status/` (22 tests, green),
TESTING.md rewritten to the real workflow. Run: `pio test -e native`.

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

### Batch A — Pilot: Trivial R/W Registers (SLEEP_CONTROL, CUSTOMER_USE) — ✅ DONE 2026-07-06

**Completed on branch `feat/registers-batch-a`** (delegated to a Sonnet
`general-purpose` subagent in an isolated worktree, orchestrator-gated). Added
`TPS25751SleepControl` (0x70) and `TPS25751CustomerUse` (0x06), full factory
integration, `read/writeSleepControl` + `read/writeCustomerUse` convenience
methods (the library's **first host-register write path**), Doxygen, and two
native Unity suites (41 tests). Warning-free `example-read-registers` build,
green native run. The `/code-review high` gate found no correctness bugs; it
surfaced four altitude/simplification findings, one fixed directly (derived a
redundant field mask) and three escalated into the plan amendments below (a
shared host-side `setBits()`/write-encoder primitive is now owed before Batch B).
See [Pilot Retrospective & Plan Amendments](#pilot-retrospective--plan-amendments-batch-a).

- **Why first register batch:** Smallest possible surface (1 and 8 bytes, no sub-structure to
  speak of). Their purpose here is to *validate the delegated
  implement-then-review pipeline end-to-end* — work-order format, subagent
  conventions, review gate, doc updates — at minimal cost. If the pipeline
  produces bad output here, fix the pipeline before Batch B.
- SLEEP_CONTROL: parse sleep-mode enable/config bits; read + write path.
- CUSTOMER_USE: generic 8-byte application-defined blob; hex accessor +
  raw read/write; interpretation left to the application.

### Batch B — Interrupt Control (INT_MASK1, INT_CLEAR1) — ✅ DONE 2026-07-07

**Completed on branch `feat/registers-batch-b`** (delegated to a Sonnet
`general-purpose` subagent in an isolated worktree, orchestrator-gated). Added
`TPS25751InterruptMask` (0x16, R/W) and `TPS25751InterruptClear` (0x18,
write-only), full factory integration, `readInterruptMask` /
`writeInterruptMask` / `writeInterruptClear` convenience methods, Doxygen, and two
native Unity suites. **Prerequisite amendments 1 & 2 landed first** (orchestrator,
not delegated): the base-class `setBits()` write primitive + typed
`writeRegister<T>()` keyed on a class-level `kAddress`, with `TPS25751SleepControl`
migrated as the worked example and `TPS25751CustomerUse` retrofitted. Warning-free
`example-read-registers` build; 103/103 native tests green. The `/code-review high`
gate (run inline per amendment 4) found **one** correctness issue: the delegated
implementation omitted bit 3 (Plug Insert or Removal), tracing to an incomplete
bit list in the orchestrator's work order (the amendment-3 failure mode) — fixed
directly, all 36 INT_EVENT1 sources now present.

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

### Batch C — Telemetry (ADC_RESULTS) — ✅ DONE 2026-07-07

**Completed on branch `feat/registers-batch-c`** (delegated to a Sonnet
`general-purpose` subagent in an isolated worktree, orchestrator-gated). Added
`TPS25751ADCResults` (0x6A, read-only), full factory integration,
`readADCResults` / `readADCResultsRegister` convenience methods, Doxygen, and a
native Unity suite (23 tests). Warning-free `example-read-registers` build; 126/126
native tests green. The `/code-review high` gate (run inline per amendment 4) found
**no** correctness or cleanup findings — the closest read-only reference
(`TPS25751GPIOStatus`) made this a near-mechanical transcription, and the
orchestrator's work order pasted the full byte→field map so the non-alphabetical
GPIO ordering (GPIO4/5/0/2) transcribed cleanly.

- **Why third:** Read-only decode, immediately useful for hardware debugging
  (VBUS/VIN/temperature measurements), and the newest addition to scope — it was
  missing from earlier plan revisions despite being in the address enum.
- Decode the 13-byte measurement block into named, unit-converted accessors;
  note in the class docs that the controller does **not** update this register
  in Unconnected-Sleep mode (stale reads are not an error).

### Batch D — Capability Encoding (TRANSMIT_SOURCE_CAPS, TRANSMIT_SINK_CAPS) — ✅ DONE 2026-07-07

**Completed on branch `feat/registers-batch-d`** (delegated to a Sonnet
`general-purpose` subagent in an isolated worktree, orchestrator-gated per the
Batch-D Tightening rules). Added `TPS25751TransmitSourceCaps` (0x32, 63 B, R/W) and
`TPS25751TransmitSinkCaps` (0x33, 53 B, R/W) — the library's first **PDO encode**
path: pure static `build{FixedSupply,Battery,Variable,PPS}PDO()` word builders for
all four PDO types plus `setPDO`/`setPDOCount`/typed setters and (source only)
`get/setPowerPathForPDO`, full factory integration,
`read/writeTransmitSourceCaps` + `read/writeTransmitSinkCaps` convenience methods,
Doxygen, and two native Unity suites (23 tests each; 175/175 total green).
Warning-free `example-read-registers` build. The work order pasted the full,
hardware-verified PDO bit map (mirroring the Received-caps decode `.cpp`, which
overrode two imprecise PPS bit ranges in CONSTRAINTS.md) and the byte-offset
difference (source PDO 1 at byte 3 vs sink at byte 1). The inline `/code-review high`
gate (run inline per amendment 4) found **no correctness findings**; the manual
per-PDO re-derivation and bit-offset cross-check both passed. The subagent also
surfaced a **latent pre-existing bug** in the shipped read-only
`TPS25751ReceivedSinkCaps::getFixedFastRoleSwapCurrent()` (returns `bool` for a
2-bit field, truncating 2/3→1); it correctly modeled the new R/W sink getter as
`uint8_t` and the trap was fed into CODE_REVIEW_GUIDELINES.md + CONSTRAINTS.md.
The anticipated shared PDO encode/decode utility was **deliberately not extracted**
(an architecture refactor of two working decode classes right before an
auto-publish release; deferred as an optional future cleanup — the two new classes
mirror decode self-contained, matching the existing Received Source/Sink
duplication precedent).

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

### Batch E — Negotiation & I/O Configuration (AUTONEGOTIATE_SINK, IO_CONFIG) ✅ done 2026-07-07

- AUTONEGOTIATE_SINK (24 bytes): auto-negotiation preferences — voltage/current
  ranges and priorities; R/W with range validation.
- IO_CONFIG (49 bytes): per-pin GPIO configuration. **Correction during
  implementation:** the TRM/register-docs mark every IO_CONFIG field `[R]` and
  state the register "cannot be modified at run-time" (GPIO configuration is
  fixed at boot) — this plan's "R/W" above was wrong; the register was
  implemented **read-only**, with no setters/write path/`kAddress`. Its bit
  offsets also run up to 391, past `extractBits()`'s `uint8_t bitPos` range, so
  every field is decoded via direct byte assembly from `_raw[]` (mirroring
  `TPS25751TransmitSourceCaps::getPDO()`) instead of `extractBits()`.

### Batch F — Niche (LIQUID_DETECTION_CONFIG) ✅ done 2026-07-08

**Completed on branch `feat/registers-batch-f`** (delegated to a Sonnet
`general-purpose` subagent in an isolated worktree, orchestrator-gated). Added
`TPS25751LiquidDetectionConfig` (0x98, 11 B, R/W) — liquid/moisture detection
thresholds and response actions: with/without-liquid sleep intervals (1000 ms/LSB),
ADC sample count and sample-time cadence (10 ms/LSB), two ADC threshold pairs
(14 mV/LSB, each providing hysteresis for exit out of Liquid Detected), and the
liquid-detection-state / enable-corrosion-mitigation / enable-liquid-detection bits.
Structurally a near-clone of `TPS25751AutoNegotiateSink`: `setBits()`-based field
setters, `kAddress` + typed `writeRegister<T>()` write path, and a cross-field
semantic check (low ≤ high on each threshold pair). All bit offsets (≤ 82) fit
within `extractBits()`/`setBits()`'s `uint8_t bitPos` range, so no IO_CONFIG-style
direct byte assembly was needed. Full factory integration, `readLiquidDetectionConfig`
/ `writeLiquidDetectionConfig` / `readLiquidDetectionConfigRegister`, read-only
example demo, Doxygen, and a 32-test native suite (269/269 total green). Warning-free
`example-read-registers` build. The inline `/code-review high` gate found **no
correctness, cleanup, altitude, or convention findings**; the manual bit-offset
cross-check against the MCP register data matched all 12 fields exactly. The TRM's
"Number of Samples must be 2^N" note is intentionally **not** enforced in validation
(the all-zero reset is not a power of two). **This batch completes all 27 registers.**

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
   - The build command that must pass: `pio run -e example-read-registers`,
     and the test command that must be green: `pio test -e native` (new decode
     logic needs a suite under `test/test_native/` per TESTING.md, and its
     source file added to the native env's `build_src_filter`);
   - The Common Mistakes list from AGENTS.md, verbatim.
2. **Implementation (subagent).** Spawn with `isolation: "worktree"` so the
   subagent works on an isolated copy; it implements, builds, and reports.
3. **Quality gate (orchestrator).** On the returned diff:
   - Run **`/code-review` at high effort** — findings adjudicated against
     [CODE_REVIEW_GUIDELINES.md](../engineering/CODE_REVIEW_GUIDELINES.md);
   - Re-verify the build compiles **without warnings** and `pio test -e native`
     is green;
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

## Pilot Retrospective & Plan Amendments (Batch A)

Batch A was the pilot whose explicit job was to validate the delegated pipeline
and surface its weaknesses. It succeeded on the first delegated attempt (no
correctness findings, green build + tests), but running it surfaced concrete
gaps. Recorded here so Batches B–F benefit.

### What worked

- **Pasting MCP register facts into the work order** (rather than trusting the
  subagent to query MCP) — the subagent never needed the MCP server and the
  bit layout was correct. Keep doing this.
- **Naming the closest reference class** (`TPS25751PortControl`,
  `TPS25751Data`) and the *real* base-class API (vs the aspirational STANDARDS.md
  template) up front. Without the explicit "use `isSemanticallyValid()`, not
  `validateBasic/Data/Semantic`" note the subagent would likely have followed the
  template and produced non-compiling code.
- **The orchestrator pre-empting known traps in the work order** (CUSTOMER_USE's
  all-zero reset vs `hasValidData()`; reserved-bit preservation in setters). The
  subagent even caught and corrected a factual error in the work order's
  `hasValidData()` description rather than coding to the wrong spec — a healthy
  sign, but see amendment 3.

### Weaknesses found & amendments

1. **No shared host-side write-encoder primitive (blocking for Batch B).**
   Batch A is the library's first host register write path and it hand-rolls
   read-modify-write bit-poking with per-class masks. STANDARDS.md's
   write-encoder pattern mandates `setField8`/`setField16BE` helpers, but those
   live only in the sibling `lib/BQ25798` repo — the host side has no
   `setBits(bitPos, bitLen, value)` symmetric to the base-class `extractBits()`.
   For a 1-byte/2-field register this is fine; replicated across INT_MASK1 (11 B,
   ~30 fields), IO_CONFIG (49 B), AUTONEGOTIATE_SINK (24 B) it is a transcription-
   error factory on live writes.
   **Amendment:** before Batch B, the **orchestrator** (this is
   architecture/base-class work — *not delegated*, per "When Not to Delegate")
   adds a `setBits*` write primitive to `TPS25751Register` (mirroring
   `extractBits*`), migrates `TPS25751SleepControl`'s setters to it as the worked
   example, and documents it in STANDARDS.md. Batch B's work order then points
   subagents at that primitive.
   **✅ Delivered (Batch B, 2026-07-07):** `TPS25751Register::setBits()` (instance
   + static, symmetric to `extractBits`), SleepControl setters migrated, STANDARDS.md
   "Host-register write path (Batch B onward)" section rewritten to mandate it.

2. **Per-register write convenience methods have no typed class↔address coupling.**
   `writeSleepControl`/`writeCustomerUse` are identical size-check-then-write
   boilerplate; the `isValidSize` guard cannot catch pairing a class with a
   wrong same-size register (INT_MASK1 and LIQUID_DETECTION_CONFIG are both 11 B).
   **Amendment:** add a `static constexpr TPS25751Registers::Address kAddress`
   (or `kRegisterInfo`) to each R/W register class and a single templated
   `TPS25751::writeRegister<T>(const T&)` keyed on it, making mis-pairing
   unrepresentable — mirroring the downstream tier's ADR-009 pattern. Fold into
   the amendment-1 orchestrator task. Retrofit the two Batch A classes.
   **✅ Delivered (Batch B, 2026-07-07):** `kAddress` on SleepControl/CustomerUse
   (and the new InterruptMask/InterruptClear), templated `TPS25751::writeRegister<T>()`;
   `writeSleepControl`/`writeCustomerUse` now forward to it.

3. **The work order can carry factual errors into the subagent.** The
   orchestrator's `hasValidData()` description was wrong; the subagent happened to
   verify against source and correct it, but a less careful run would have coded
   to the wrong spec.
   **Amendment:** work orders must phrase base-class behavioral claims as
   "verify against `<file>`" rather than as fact, and the review gate must
   diff the subagent's actual behavior against the *source*, not against the
   work order's description of it.

4. **`/code-review` finder fan-out is a resource risk.** During the Batch A gate,
   a session/rate limit killed 7 of 8 review finder sub-agents mid-flight; the
   orchestrator completed the correctness review inline instead. The gate still
   held (the orchestrator had the full diff in context), but a review that
   *depends* on fan-out completing is fragile.
   **Amendment:** the orchestrator must be prepared to run the correctness pass
   inline when fan-out is unavailable, and should keep the diff small enough
   (one batch) that this is feasible. For the larger batches (D, E) budget the
   review explicitly and prefer the deeper `/code-review ultra` gate the human
   runs pre-merge.

5. **Worktree branch base.** The subagent's fresh worktree branched from a point
   behind `main` and it had to rebase onto `main` (to pick up Batch 0's harness)
   before starting. It handled this correctly, but the work order should state
   the required base branch explicitly.
   **Amendment:** work orders name the base commit/branch the worktree must sit
   on (`main` at or after Batch 0), so the subagent doesn't have to infer it.

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

> Superseded on 2026-11-18 by the "Git Workflow" section of AGENTS.md. Merges now go
> through `scripts/verify.py` and `scripts/integrate.py`, and the release level is
> derived from Conventional Commits; `#minor` tokens have no effect. The text below is
> kept as the historical record of how Batches 0 to F were run.

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
| 0.6 | 2026-07-06 | Claude Code | **Batch 0 executed and complete**: native Unity harness (`pio test -e native`) with Arduino stubs in `test/support/native/`, seed suite for TPS25751Status (22 tests green), hardware envs restructured under `[teensy_base]` (behavior unchanged), TESTING.md rewritten to the real workflow; wired the native test command into the per-batch loop |
| 0.7 | 2026-07-06 | Claude Code | **Batch A executed and complete** (19/27, 8 remaining): `TPS25751SleepControl` (0x70) + `TPS25751CustomerUse` (0x06) via the delegated pipeline (Sonnet subagent, orchestrator-gated) — the library's first host-register write path; added the Pilot Retrospective & Plan Amendments section (shared host-side `setBits()` primitive + typed `writeRegister<T>` owed to the orchestrator before Batch B); moved both registers to Implemented and updated all counts |
| 0.8 | 2026-07-07 | Claude Code | **Batch B executed and complete** (21/27, 6 remaining): delivered the owed prerequisite amendments 1 & 2 (base-class `setBits()` write primitive + typed `writeRegister<T>()` keyed on `kAddress`; SleepControl migrated, CustomerUse retrofitted; STANDARDS.md rewritten), then `TPS25751InterruptMask` (0x16) + `TPS25751InterruptClear` (0x18) via the delegated pipeline (Sonnet subagent, orchestrator-gated). Quality gate caught one omitted source (bit 3 Plug Insert/Removal, from a work-order bit-list error) — fixed. Moved both registers to Implemented, updated all counts, marked amendments 1 & 2 delivered |
| 0.9 | 2026-07-07 | Claude Code | **Batch C executed and complete** (22/27, 5 remaining): `TPS25751ADCResults` (0x6A, read-only internal-ADC telemetry) via the delegated pipeline (Sonnet subagent, orchestrator-gated) — raw + unit-converted (14/98 mV, 16.5 mA per LSB) accessors, `readADCResults` / `readADCResultsRegister`, 23-test native suite. Work order pasted the full byte→field map (incl. the non-alphabetical GPIO4/5/0/2 ordering); the inline `/code-review high` gate found no correctness or cleanup findings. Added the Unconnected-Sleep stale-read caveat to CONSTRAINTS.md. Moved ADC_RESULTS to Implemented, updated all counts |
| 1.0 | 2026-07-07 | Claude Code | **Batch D executed and complete** (24/27, 3 remaining): `TPS25751TransmitSourceCaps` (0x32, 63 B) + `TPS25751TransmitSinkCaps` (0x33, 53 B) via the delegated pipeline (Sonnet subagent, orchestrator-gated, Batch-D Tightening applied) — the library's first **PDO encode** path: static `build*PDO()` word builders for all four PDO types (PPS at 100 mV/50 mA), `setPDO`/`setPDOCount`, source-side `get/setPowerPathForPDO`, `read/writeTransmit{Source,Sink}Caps`, two 23-test native suites (175/175 green). Work order pasted the hardware-verified PDO bit map + the source-vs-sink byte-offset difference (PDO 1 at byte 3 vs byte 1). Inline `/code-review high` gate + manual per-PDO re-derivation found **no correctness findings**. Gate surfaced a latent `bool`-truncation bug in the pre-existing read-only `TPS25751ReceivedSinkCaps` FRS getter (left unchanged; fed into CODE_REVIEW_GUIDELINES.md + CONSTRAINTS.md as a follow-up); corrected two imprecise PPS bit ranges in CONSTRAINTS.md. Shared PDO codec extraction deliberately deferred. Moved both registers to Implemented, updated all counts |
| 1.1 | 2026-07-07 | Claude Code | **Batch E executed and complete** (26/27, 1 remaining): `TPS25751AutoNegotiateSink` (0x37, 24 B, R/W sink-negotiation configuration — voltage window, minimum required power, PPS sink request parameters, `setBits()`-based setters, cross-field min≤max semantic check) + `TPS25751IOConfig` (0x5C, 49 B) via the delegated pipeline (Sonnet subagent, orchestrator-gated). **Correction:** IO_CONFIG was implemented **read-only**, not R/W as this plan previously stated — the TRM/register-docs mark every field `[R]` and state GPIO configuration is fixed at boot; no setters/write path/`kAddress` were added. IO_CONFIG is also the library's first register whose bit offsets (up to 391) exceed `extractBits()`'s `uint8_t bitPos` range, so every field decodes via direct byte assembly from `_raw[]` (mirroring `TPS25751TransmitSourceCaps::getPDO()`). Two native test suites added (61 tests). Moved both registers to Implemented, corrected IO_CONFIG's access in the Unimplemented→Implemented move, updated all counts |
| 1.2 | 2026-07-08 | Claude Code | **Batch F executed and complete — register set now 27/27, 0 remaining**: `TPS25751LiquidDetectionConfig` (0x98, 11 B, R/W liquid/moisture-detection configuration — with/without-liquid sleep intervals at 1000 ms/LSB, ADC sample count + sample-time cadence at 10 ms/LSB, two 14 mV/LSB ADC threshold pairs with hysteresis, and the liquid-detection-state / enable-corrosion-mitigation / enable-liquid-detection bits) via the delegated pipeline (Sonnet subagent, orchestrator-gated). A near-clone of `TPS25751AutoNegotiateSink`: `setBits()`-based setters, `kAddress` + typed `writeRegister<T>()` write path, cross-field low ≤ high semantic check on each threshold pair; all bit offsets (≤ 82) fit `extractBits()`/`setBits()`'s `uint8_t bitPos` range so no direct byte assembly was needed. Full factory integration, read/write + factory-reader convenience methods, read-only example demo, 32-test native suite (269/269 green), warning-free build. Inline `/code-review high` gate found **no findings**; manual bit-offset cross-check matched all 12 fields. The "Number of Samples must be 2^N" TRM note intentionally not enforced (reset 0 is not a power of two). Moved LIQUID_DETECTION_CONFIG to Implemented, updated all counts — **all planned register work is complete** |

---

## Appendix A: Register Quick Reference

| Reg# | Name | Addr | Size | R/W | Batch | Status |
|------|------|------|------|-----|-------|--------|
| 1 | MODE | 0x03 | 4 | R | — | ✅ Done |
| 2 | CUSTOMER_USE | 0x06 | 8 | R/W | A | ✅ Done (2026-07-06) |
| 3 | COMMAND | 0x08 | 4 | W | — | ✅ Done (2026-06) |
| 4 | DATA | 0x09 | 64 | R/W | — | ✅ Done (2026-06) |
| 5 | INT_EVENT1 | 0x14 | 11 | R | — | ✅ Done |
| 6 | INT_MASK1 | 0x16 | 11 | R/W | B | ✅ Done (2026-07-07) |
| 7 | INT_CLEAR1 | 0x18 | 11 | W | B | ✅ Done (2026-07-07) |
| 8 | STATUS | 0x1A | 5 | R | — | ✅ Done |
| 9 | POWER_PATH_STATUS | 0x26 | 5 | R | — | ✅ Done |
| 10 | PORT_CONFIGURATION | 0x28 | 17 | R | — | ✅ Done |
| 11 | PORT_CONTROL | 0x29 | 4 | R/W | — | ✅ Done (Phase 1) |
| 12 | BOOT_STATUS | 0x2D | 5 | R | — | ✅ Done |
| 13 | RECEIVED_SOURCE_CAPABILITIES | 0x30 | 53 | R | — | ✅ Done (Phase 1) |
| 14 | RECEIVED_SINK_CAPABILITIES | 0x31 | 53 | R | — | ✅ Done (Phase 1) |
| 15 | TRANSMIT_SOURCE_CAPABILITIES | 0x32 | 63 | R/W | D | ✅ Done (2026-07-07) |
| 16 | TRANSMIT_SINK_CAPABILITIES | 0x33 | 53 | R/W | D | ✅ Done (2026-07-07) |
| 17 | ACTIVE_PDO_CONTRACT | 0x34 | 6 | R | — | ✅ Done (Phase 1) |
| 18 | ACTIVE_RDO_CONTRACT | 0x35 | 16 | R | — | ✅ Done (Phase 1) |
| 19 | AUTONEGOTIATE_SINK | 0x37 | 24 | R/W | E | ✅ Done (2026-07-07) |
| 20 | POWER_STATUS | 0x3F | 2 | R | — | ✅ Done |
| 21 | PD_STATUS | 0x40 | 4 | R | — | ✅ Done |
| 22 | IO_CONFIG | 0x5C | 49 | R *(corrected from R/W)* | E | ✅ Done (2026-07-07) |
| 23 | TYPEC_STATE | 0x69 | 4 | R | — | ✅ Done |
| 24 | ADC_RESULTS | 0x6A | 13 | R | C | ✅ Done (2026-07-07) |
| 25 | SLEEP_CONTROL | 0x70 | 1 | R/W | A | ✅ Done (2026-07-06) |
| 26 | GPIO_STATUS | 0x72 | 8 | R | — | ✅ Done (Phase 1) |
| 27 | LIQUID_DETECTION_CONFIG | 0x98 | 11 | R/W | F | ✅ Done (2026-07-08) |

**Note:** For USB PD PDO type details, see [CONSTRAINTS.md - USB Power Delivery Complexity](../engineering/CONSTRAINTS.md#usb-power-delivery-complexity)

---

*End of Implementation Plan v1.2*
