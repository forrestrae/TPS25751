# Plan: Add the ten unsupported TPS25751 rev B registers

## Context

The library decodes 27 of the 37 registers in TRM SLVUCR8B (Rev. B, 2026-08). The rev B audit (`docs/plans/trm-revB-audit.md`, "Out of scope") lists the other ten and ranks them by usefulness to a sink/source host. This plan adds all ten in five batches of two, continuing the batch structure of `docs/plans/plan-impl-v01.md`. When it is done the library covers 37 of 37 registers.

All ten are implemented. Eight are useful to any PD host: Version for diagnostics and patch checks, PD3 Configuration because it gates every Tx data block below, Global System Configuration and Received SOP Identity for diagnostics and board configuration, and the four Tx blocks the controller sends in reply to PD 3.x Get_* messages. The two battery registers (7Bh, 7Dh) matter only when the product answers Get_Battery_Status and Get_Battery_Capabilities. The test board carries a BQ25798 charger, so it is a battery-backed design, and the decision on 2026-11-18 was to implement them. They go last because they have the most new machinery (see "Decisions" under Approach).

All batches are committed on `fix/rev-b-trm`, the branch that carries the rev B audit fixes, and they ship when that branch is integrated (decision 6 under Approach). Nothing waits on an integration: Batch H's example and hardware check use `TPS25751PortControl::automaticIdRequest()` (29h bit 16, audit step 12) and `TPS25751InterruptEvent::discoverModeCompleted()` (bit 51, audit step 5), and both are already on that branch. Batches run one at a time, because every batch edits the same shared files: `include/TPS25751RegisterAddress.h`, the factory, `include/TPS25751.h`, `AGENTS.md`, `examples/read-registers/src/main.cpp`, and the native `build_src_filter` in `platformio.ini`.

### Register facts from tps25751-docs

`list_registers` returned 37 registers from SLVUCR8B. `get_register` was run for each of the ten. Widths and resets below are the server's; byte counts are the width rounded up to whole bytes. Pages 55, 57, and 58 were also checked against the rev B PDF in the server repo (`devices/tps25751/docs/`), because the server's text for 78h, 7Bh, and 7Dh looked wrong.

| Offset | MCP name | Width (bytes) | Access | Reset | Page | Field data |
|---|---|---|---|---|---|---|
| Fh | VERSION | 32 (4) | R | 00000000h | 18 | Complete. One 32-bit field; the layout is given in prose: "byte 1 = RR, byte 2 = MM, etc." |
| 42h | PD3_CONFIGURATION | 32 (4) | R/W | 00080010h | 46 | Complete. Bit 7 (Auto Get MIDB) says results land in TX_MIBDB_SOP (0x73), which is not among the server's 37 registers. TI misspellings in prose: "GetManufaturerInfo", "msesage". |
| 27h | GLOBAL_SYSTEM_CONFIGURATION | 116 (15) | R/W | 28101h | 28 | Complete. Fields occupy bits 23:0 only; bits 115:24 are reserved. The reset decodes as PP3 Config 2h, ILIM Over Shoot 2h, PP1 Config 1h, PP Cable1 Switch Config 1h, matching the per-field resets. |
| 48h | RECEIVED_SOP_IDENTITY_DATA_OBJECT | 200 (25) | R | 0 | 47 | Complete at register level. The six VDOs are opaque 32-bit words; their internal fields are defined by the USB PD specification, and the server has no data for them. |
| 78h | TX_SOURCE_INFO | 64 (8) | R/W | 8000000080000000h | 55 | Complete but ambiguous, in the TRM itself. There are two Port Type bits (63 "PortType" and 31 "Port Type"), and the TRM does not say how the upper word's 0.5 W fields relate to the lower word's 1 W fields. The description says the PD firmware updates the register automatically, yet every field is R/W. The server's field name "PortGuaranteedPDP_0p5 W" has a stray space from a line wrap in the PDF. |
| 7Ah | TRANSMITTED_PPS_STATUS_DATA_BLOCK | 32 (4) | R/W | 00000000h | 56 | Complete layout with two TRM gaps. PTF (bits 26:25) defines 01b, 10b, and 11b only; 00b, the reset value, has no stated meaning. The description calls the block "(BSDO)", a copy error from 7Bh. |
| 77h | TX_SOURCE_CAPABILITIES_EXTENDED_DATA_BLOCK | 120 (15) | R/W | 0 | 54 | Complete layout. Field encodings are "as defined by the USB PD specification" and are not in the server. The description names the 42h gate bit "SourceCapExtMsg"; 42h calls bit 8 "Support Source Extended Message". |
| 7Eh | TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK | 112 (14) | R/W | 0 | 59 | Complete layout, encodings deferred to the USB PD specification. TI spelling "Touch Temprature" (bits 47:40). |
| 7Bh | TRANSMITTED_BATTERY_STATUS_DATA_OBJECTS_(BSDO) | 128 (16) | R/W | 000000000000000000000000FFFF0200h | 57 | Complete layout. Battery 3's capacity field (bits 127:112) is named "Battery 3 Present Info"; batteries 0 to 2 use "Present Capacity". The PDF (Table 3-36) has the same text, so this is a TRM naming error. |
| 7Dh | TX_BATTERY_CAPABILITIES | 288 (36) | R/W | 0 | 58 | Layout complete, labels wrong in the TRM. Bits 287:216 are described as "battery index 0" while their fields are named PID 3 and VID 3, and four fields share each of the names "Battery Type", "Battery Design Capacity", and "Battery Last Full Charge Capacity". The PDF (Table 3-37) has the same text, so the server is faithful and the error is TI's. Name-based `explain_bitfield` lookups on this register are ambiguous; work orders must cite bit ranges. |

The server answered every query with addresses, widths, resets, access types, and pages. Nothing in this plan fills a field gap from memory. Where the TRM defers to the USB PD specification (48h VDO internals; the 77h, 7Eh, 7Bh, and 7Dh field encodings), the batches ship raw accessors, and typed decode waits for the server to serve the USB PD specification (see Batch L).

The TRM defects in the table are recorded in the server repo as seven new `open` entries in `devices/tps25751/known_document_issues.json` (2026-11-18): `7Dh-battery-3-labels`, `7Bh-battery-3-name`, `7Ah-description-and-PTF`, `78h-port-type-words`, `77h-gate-bit-name`, `42h-TX_MIBDB_SOP-undocumented` (0x73 is referenced on p.46 but documented nowhere in the TRM), and `rev-B-misspellings-2` (p.46 and p.59). Their evidence strings pass the server's PDF check. None is filed with TI yet. The stray space in the server's 78h field name "PortGuaranteedPDP_0p5 W" is an extraction artifact, not a TRM defect, so it is not in that file; it belongs in the server's registers JSON.

## Approach

### Workflow reused from plan-impl-v01

Each batch runs the v01 process unchanged except where listed below:

- Batch structure: one batch is one review gate and one set of commits, run as its own Claude Code session ("Implementation Order & Rationale" and "Launching the Plan" in v01). Unlike v01, batches do not get their own branches (workflow change 2).
- Delegated Implementation Workflow: orchestrator writes the work order, a Sonnet `general-purpose` subagent implements in an isolated worktree, the orchestrator gates with `/code-review` at high effort plus a manual bit-offset cross-check against the MCP data ("Delegated Implementation Workflow", "Roles", "Per-Batch Loop", and "When Not to Delegate" in v01).
- Pilot Retrospective amendments 1 to 5 stay in force: paste MCP facts into the work order; phrase base-class behavior as "verify against `<file>`"; be ready to run the correctness pass inline; name the base branch; use `setBits()`, `kAddress`, and `writeRegister<T>()` for every R/W register.
- Documentation Update Checklist (per batch) from v01, with the changes in "Per-batch documentation" below.

### Changes to the v01 workflow

1. Each batch adds its addresses. In v01 all 27 addresses already lived in `TPS25751Registers::Address`. None of the ten do. Each batch adds an `Address` enumerator, a `Registers::` `RegisterInfo` constant, and a case in both `getRegisterSize()` and `getRegisterInfo()` in `include/TPS25751RegisterAddress.h`. Enumerator names follow the library's style rather than the MCP names: `VERSION`, `PD3_CONFIGURATION`, `GLOBAL_SYSTEM_CONFIGURATION`, `RECEIVED_SOP_IDENTITY`, `TRANSMIT_SOURCE_INFO`, `TRANSMIT_PPS_STATUS`, `TRANSMIT_SOURCE_CAPABILITIES_EXTENDED`, `TRANSMIT_SINK_CAPABILITIES_EXTENDED`, `TRANSMIT_BATTERY_STATUS`, `TRANSMIT_BATTERY_CAPABILITIES`. Each carries a `///` comment with the TRM title and rev B page, as the INT_* entries do.
2. Every batch commits to `fix/rev-b-trm`; there are no per-batch branches or archive tags. The subagent's worktree is based on the current tip of `fix/rev-b-trm`, and the orchestrator brings its commits onto `fix/rev-b-trm` by fast-forward (`git merge --ff-only <worktree-branch>`) or cherry-pick, never by a merge commit, because `scripts/verify.py` refuses merge commits on the branch. Batch letters continue at G so they do not clash with v01's Batches A to F. Batches run one at a time, since two sessions writing in the same checkout would overwrite each other.
3. Integration and release follow AGENTS.md "Git Workflow", once for the whole branch: Conventional Commits for every batch commit, `scripts/verify.py` on a clean tree after each batch as a check, and after the last batch `scripts/integrate.py fix/rev-b-trm` and one human push (see "Release level"). The `#minor` token in v01's "Merge & release" no longer has any effect.
4. Work orders cite rev B table numbers and pages, apply the audit's "Appendix B: Spelling of TI's misspelled names" policy (correct spelling in identifiers, TI's spelling quoted once in the doc comment), and identify fields by bit range wherever the TRM reuses a name (78h, 7Dh).
5. Each new `.cpp` is added to `[env:native]` `build_src_filter` in `platformio.ini`.
6. Kickoff prompts are v01's template with `trm-revB-audit-remaining-registers.md` substituted and `fix/rev-b-trm` as the working branch, plus the implementer's model and effort from "Models and effort" below. Batch K gets the Batch-D Tightening reminder (see Batch K).
7. Models and effort are fixed per batch rather than left to the session. v01 named only model classes ("Fable/Opus class" orchestrator, Sonnet implementer, Haiku for Batch A only) and set no effort levels.

### Models and effort

The split stays as v01 proved it: a stronger model orchestrates and gates, Sonnet implements. Batches A to F all passed the gate on Sonnet output, and the per-register work here is the same formulaic transcription, so a stronger implementer would add cost without removing a known failure mode. Effort rises where the batch adds something the library has not done before: unit conversion with sentinel values (Batch I) and R/W byte assembly past bit 255 (Batch K).

| Batch | Orchestrator session (model, effort) | Implementer subagent (Agent `model`, `effort`) | Review gate |
|---|---|---|---|
| G | Opus 5.5 (`claude-opus-5-5`), high | `sonnet` (Sonnet 5.5), `medium` | `/code-review high` |
| H | Opus 5.5, high | `sonnet`, `medium` | `/code-review high` |
| I | Opus 5.5, high | `sonnet`, `high` | `/code-review high` |
| J | Opus 5.5, high | `sonnet`, `medium` | `/code-review high` |
| K | Opus 5.5, high | `sonnet`, `high` | `/code-review xhigh`; the human may also run `/code-review ultra` before `fix/rev-b-trm` is integrated |
| L | Opus 5.5, high | `sonnet`, `high` | `/code-review high`, plus a hand re-derivation of one decoded value per spec table, as v01's Batch-D Tightening required for PDOs |

- Set the orchestrator's model and effort when starting the batch session (the app's model selector, or `claude --model claude-opus-5-5` with effort high). The orchestrator writes the work order, runs the gate, does the manual bit-offset cross-check, and owns every task v01's "When Not to Delegate" reserves for it, including the 7Dh write helpers if a gate finding pushes them toward the base class.
- The orchestrator passes the implementer's `model` and `effort` explicitly on the Agent call, with `isolation: "worktree"`. This plan is the instruction that authorizes setting `effort`; without it the subagent runs at its default.
- Haiku is not used. v01 allowed it for the Batch A pilot only, and Batch G, the cheapest batch here, still introduces a new address-table step (workflow change 1) that the pilot never exercised.
- Fable 5.2 is not used by default. Escalate the orchestrator to Fable 5.2 for a batch only if that batch hits v01's escalation rule (two failed gates with correctness findings) and the orchestrator is taking over implementation.
- If the escalation rule fires, the orchestrator implements at the same model and effort as its session; it does not start a third Sonnet attempt.

### Batches and order

| Batch | Registers | Shared machinery | Why this position |
|---|---|---|---|
| G | Fh Version, 42h PD3 Configuration | 4-byte registers, single `extractBits`/`setBits` fields | Cheapest and highest value. 42h is the enable for 77h, 78h, 7Ah, 7Bh, 7Dh, and 7Eh, so later batches cite its accessors. |
| H | 27h Global System Configuration, 48h Received SOP Identity | Non-standard sizes (15 B from 116 bits, 25 B); 27h R/W, 48h read-only | Diagnostics and board configuration. The 27h read also shows whether the test board can source power, which Batch I's hardware check needs. |
| I | 78h TX Source Info, 7Ah Transmitted PPS Status | Source-side R/W info words with unit conversion (0.5 W, 1 W, 20 mV, 50 mA) and "not supported" sentinels | Source-role replies, ranked next in the audit. |
| J | 77h Tx Source Caps Extended, 7Eh Transmit Sink Caps Extended | Byte-aligned R/W extended-capability blocks of raw 8- and 16-bit fields | Same shape and the same "USB PD defines the encoding" treatment. |
| K | 7Bh BSDO, 7Dh Tx Battery Capabilities | Indexed per-battery entries; 7Dh is the first R/W register past bit 255 | Battery-backed designs. Last, because it has the most new machinery. |
| L | Typed decode for 48h, 77h, 7Eh, 7Bh, 7Dh, and the 7Ah PTF meaning | USB PD R3.3 tables served by the `usb-pd-docs` MCP server | Runs because Batches H, J, and K shipped before a server served the USB PD specification. |

### Decode technique

`TPS25751Register::extractBits*()` and `setBits()` take a `uint8_t bitPos` (`include/TPS25751Register.h:76-120`). Of the ten, only 7Dh has a field above bit 255. The highest field bit in each of the others is: Fh 31, 42h 20, 27h 23 (reserved to 115), 48h 199, 78h 63, 7Ah 27, 77h 110 (reserved to 119), 7Eh 87 (reserved to 111), 7Bh 127. Those nine use `extractBits*()` and `setBits()` directly. For 48h, the highest call is `extractBits32(168, 32)`, which ends at bit 200, the end of the 25-byte buffer.

7Dh (288 bits) holds four 72-bit battery entries. Battery 3 occupies bits 287:216, so its Design Capacity (bit 248, ending at 264), Last Full Charge Capacity (bit 264), and Battery Type (bit 280) cannot be addressed with a `uint8_t bitPos`. Every 7Dh field is byte-aligned. The class decodes and encodes all four entries by direct byte assembly from `_raw[]` with a 9-byte stride, following `TPS25751IOConfig::readGpioBitmap()` (`src/TPS25751IOConfig.cpp:3`) for the bounds-checked little-endian read. IO_CONFIG is read-only, so 7Dh also needs the library's first write past bit 255: class-private `readLE16()`, `writeLE16()`, and byte-write helpers guarded by `isValid()` and a bounds check. Widening the base class's `bitPos` to `uint16_t` would remove the need for those helpers, but it is a base-class API change, which v01's "When Not to Delegate" reserves for the orchestrator and which would deserve an ADR. This plan does not make that change.

27h is 116 bits. Its `RegisterInfo` size is 15 bytes, the width rounded up, which is the rule LIQUID_DETECTION_CONFIG followed (83 bits, 11 bytes) until a bench read on 2026-11-18 showed a length byte of 12 there too; it is now 12 bytes. Bits 119:116 (the high nibble of byte 14) lie outside the register: the class does not decode them, validation does not check them, and no setter touches them. Writes are read-modify-write, so `writeGlobalSystemConfig()` sends the nibble back exactly as it was read; a default-constructed object sends 0. One hardware risk remains. `TPS25751::readRegister()` rejects any response whose length byte differs from the expected size (`src/TPS25751.cpp:50`). If the controller reports a count other than 15 for 27h, every read fails. Batch H confirms the length byte on hardware before the batch is marked done. If it differs, set `RegisterInfo` to the observed count and record it in CONSTRAINTS.md, as was done for ACTIVE_RDO_CONTRACT (16 B).

### Access and write paths

| Register | TRM access | Library | Setters, `kAddress`, `TPS25751` write method |
|---|---|---|---|
| Fh Version | R | Read-only | None |
| 42h PD3 Configuration | R/W | R/W | Yes: `writePD3Config()` |
| 27h Global System Configuration | R/W | R/W | Yes: `writeGlobalSystemConfig()` (decision 2). Doxygen on the class and the write method quotes the TRM: "Any modifications to this register will cause a port disconnect and reconnect". 28h PORT_CONFIGURATION stays decode-only; this plan does not change it. |
| 48h Received SOP Identity | R | Read-only | None |
| 78h TX Source Info | R/W | R/W | Yes: `writeTransmitSourceInfo()`. Doxygen states that the firmware may overwrite host writes. |
| 7Ah Transmitted PPS Status | R/W | R/W | Yes: `writeTransmitPPSStatus()` |
| 77h Tx Source Caps Extended | R/W | R/W | Yes: `writeTransmitSourceCapsExtended()` |
| 7Eh Transmit Sink Caps Extended | R/W | R/W | Yes: `writeTransmitSinkCapsExtended()` |
| 7Bh BSDO | R/W | R/W | Yes: `writeTransmitBatteryStatus()` |
| 7Dh Tx Battery Capabilities | R/W | R/W | Yes: `writeTransmitBatteryCapabilities()` |

Every register also gets `read<Name>Register(bool validate = true)`, and every R/W register gets the `read<Name>()` / `write<Name>()` pair, mirroring `readLiquidDetectionConfig` / `writeLiquidDetectionConfig` / `readLiquidDetectionConfigRegister`. Write methods forward to `TPS25751::writeRegister<T>()`.

### Cross-register dependencies

- 42h gates the Tx blocks. Bit 8 enables replies from 77h, bit 10 from 7Dh, bit 11 from 7Bh, bit 17 from 7Eh, bit 18 from 78h, and bit 20 from 7Ah. With the bit clear, the controller answers the corresponding Get_* message with Not_Supported. Each Tx class's Doxygen names its gate bit and the 42h accessor.
- 48h is filled when the controller sends Discover Identity. PORT_CONTROL Automatic ID Request (29h bit 16, reset 1h) makes that automatic. INT_EVENT1 bit 51 means "The Discover Modes process has completed" (p.19). That is a later step in discovery, not a signal that 48h changed, and INT_EVENT1 has no Discover Identity event (the server lists 38 named bits, none for identity). The 48h Response Type field is therefore the authoritative status; bit 51 is a hint whose timing Batch H records on hardware.
- 77h's two battery-count fields should agree with the populated entries in 7Bh and 7Dh (TRM p.57 and p.58: "The host should also program the Tx Source Capabilities Extended register (0x77) ... consistent with the contents of this register"). Single-register validation cannot check this, so the rule goes in Doxygen on all three classes.
- 77h and 7Eh hold only the tail of the PD data block. The controller prepends VID, PID, and XID from firmware configuration, the firmware version, and the hardware version from BOOT_FLAGS (2Dh) REV_ID.
- 42h bit 7 (Auto Get MIDB) stores its result in TX_MIBDB_SOP (0x73), which neither the library nor the server covers. Its companion event is INT_EVENT1 bit 29, MIDB Received.

### Validation tiers

The library's real tiers are basic (`isValid()`, `isValidSize()`), data (`hasValidData()`, which rejects the all-0xFF bus-error signature), and semantic (`isSemanticallyValid()`). Basic and data come from the base class for all ten registers. Each register's semantic rules are listed in its batch. Two rules apply throughout:

- Reserved ranges must read 0. Every reserved range in the ten registers has reset 0h. If hardware shows firmware setting a reserved bit, drop that check and record why in CONSTRAINTS.md.
- A documented reset value must pass validation unless it actually violates a stated rule. Each suite includes a reset-fixture test.

### Hardware validation

The test board is a Teensy 4.0 host and a TPS25751 in Application mode with an EEPROM configuration (audit step 1). Connection states are: none; attached source (a PD charger, board sinking); attached sink (board sourcing, if 27h shows a source path). The controller's replies to Get_Source_Info, Get_PPS_Status, Get_*_Cap_Extended, Get_Battery_*, and Get_Revision are visible only to a PD partner or a PD analyzer. Without one, that behavior is unit-tested only, and hardware checks for those registers are limited to reads and write/read-back.

| Register | Connection state | Hardware check | Unit-test only |
|---|---|---|---|
| Fh | Any | Read; compare with the firmware version from TI's tooling if available | Byte-order decode beyond the observed value |
| 42h | Any | Read; write one responder bit, read back, restore | Effect on PD replies |
| 27h | Read: any. Write: no partner attached, board powered independently of the USB-C port | Read; confirm the length byte is 15 and decode the board's PP1, PP3, and RCP settings. Write: change RCP Threshold, read back, observe the disconnect and reconnect, restore the original value | PP1 and PP3 writes (a wrong PP3 role can back-drive the power path), undefined PP1/PP3 codes |
| 48h | None, then attached source, then attached sink if available | None: Response Type 0, count 0. Attached: Response Type and VDO count after discovery; time of INT_EVENT1 bit 51 relative to the 48h update | NAK and BUSY decode unless a partner produces them |
| 78h | None, then attached sink | Read; check whether the lower word changes when the board sources; write the upper word and read back | Get_Source_Info reply |
| 7Ah | Any | Write and read back | Get_PPS_Status reply |
| 77h, 7Eh | Any | Write and read back; on 77h, note whether Source Inputs bit 0 tracks Barrel_Jack_Event | Get_*_Cap_Extended replies |
| 7Bh | Any | Read the reset (expected FFFF0200h in the low word); write and read back | Get_Battery_Status reply |
| 7Dh | Any | Write all four entries, including battery 3 past bit 255, and read back | Get_Battery_Capabilities reply |

### Release level

`scripts/release_level.py` takes the highest level among non-merge commits since the last `chore(release)` commit: a breaking marker gives major, any `feat` gives minor, anything else gives patch. Each batch adds public API in `feat(...)` commits and breaks nothing: the new enumerators were not previously representable, so no existing call changes behavior. Each batch therefore carries a minor level.

The whole branch ships as one release, from one push (decision 4). Publishing runs only in CI on a push to `main` (`.github/workflows/publish.yml`), CI derives one release level per push, and the `pre-push` hook refuses pushes from agent shells, so every release takes a human push. After the last batch, `scripts/integrate.py fix/rev-b-trm` merges the branch into `main` and tags `archive/fix/rev-b-trm`. Then the human pushes both in one command, from this library root:

```bash
git push origin main refs/tags/archive/fix/rev-b-trm
```

CI runs the verify job once on the final tip and publishes one major release: 3.0.0 from the current 2.0.2. The batches alone would make it minor, since the branch carries `feat` commits from the audit fixes and from every batch. The removal of `writeCustomerUse()` after Batch K (CUSTOMER_USE is read-only at runtime in rev B) is a breaking change marked `!`, which makes it major.

### Decisions

Decided on 2026-11-18:

1. Batch K is implemented, not deferred.
2. 27h gets setters, `kAddress`, and `writeGlobalSystemConfig()` despite the disconnect-on-write behavior. Examples never call the write method.
3. Typed decode from the USB PD specification comes from the MCP server, which is being extended to serve it. A batch whose work order can paste the spec tables from the server includes typed decode; a batch that starts before then ships raw accessors, and Batch L adds the typed decode later.
4. One release from one push at the end, as described under "Release level".
5. The TRM defects are recorded in the server repo's `known_document_issues.json` (see "Register facts from tps25751-docs").
6. The batches are committed on `fix/rev-b-trm` and ship together with the rev B audit fixes. This plan lives at `docs/plans/trm-revB-audit-remaining-registers.md`.

### Per-batch documentation

v01's "Documentation Update Checklist (per batch)" applies with these changes:

- Edit `docs/plans/trm-revB-audit-remaining-registers.md`, not v01. v01 stays as the historical record. Record each batch's outcome the way `trm-revB-audit.md` records its steps: a status tag on the batch heading (`[done]`, `[partial]`, or `[not started]`) and a Verified line naming the checks run and the commits.
- `AGENTS.md`: the "Last Updated" and "Current Phase" lines, the "Implemented Registers" count in Implementation Status (27/37 before Batch G, then 29, 31, 33, 35, 37), the "Status" line under it, the Register Classes list (one entry per new class), and the documentation tree's plan line, which still says "12 registers remaining". In Batch G only: point "Current Roadmap" at this plan, and change the MCP table's "27 registers" to 37. That column describes the server, which serves 37 registers whatever the library supports.
- `docs/plans/trm-revB-audit.md` "Out of scope", in Batch G only: link the register table to this plan.
- `docs/engineering/STANDARDS.md` "Factory Integration" step 1, in Batch G only: it says the 27 addresses already exist. Rewrite it to say a new register adds its `Address`, `RegisterInfo`, and both lookup cases.
- Factory: forward declaration in `include/TPS25751RegisterFactory.h`, two private creators in `TPS25751RegisterFactoryImpl`, and one case in each of the three `createRegister()` switches in `src/TPS25751RegisterFactory.cpp` (81 cases today, 111 after Batch K).
- `examples/read-registers/src/main.cpp`: a read-only demo of every new register. No example writes a register. Update `examples/README.md` and `examples/AGENTS.md` when the demonstrated list changes.
- `docs/engineering/CONSTRAINTS.md`: entries named in each batch.
- `docs/engineering/CODE_REVIEW_GUIDELINES.md`: any new bug pattern the gate catches.
- `README.md`: a Highlights bullet only when a batch adds a user-visible capability (Batch J or K: "PD 3.x extended and battery data replies").

## Changes

### Batch G: Version (Fh) and PD3 Configuration (42h) [done]

Both registers are 4 bytes with fields well inside `extractBits`/`setBits` range.

- Status: done. Hardware checks 2 and 3 passed on 2026-11-18, check 2 after a validation fix. Check 1, the VERSION byte layout, was compared with TI's tooling on 2026-11-19 and closed without a decode change; commit 965a975 records the comparison in the `TPS25751Version` header and AGENTS.md.
- Verified: `pio test -e native` 404/404, with new suites `test_TPS25751Version` (15 tests) and `test_TPS25751PD3Config` (28 tests). `pio run -e example-read-registers` compiled all 125 units with the build cache bypassed, with no warnings. `scripts/verify.py` passed at b02e0ca. `release_level.py main..fix/rev-b-trm` reports minor. The manual cross-check against MCP `get_register(VERSION)` and `get_register(PD3_CONFIGURATION)` matched every field: Version bits 31:0 split as 31:16, 15:8, and 7:0; PD3 bits 20, 19, 18, 17, 11, 10, 8, 7, and 4; reserved mask 0xFFE1F26F; 4-byte `RegisterInfo` at 0x0F and 0x42. Tables 3-7 (p.18) and 3-26 (p.46) are confirmed in the rev B PDF. Commits 256b7f3 (Version), 14e9b39 (PD3 Configuration), d3bf4b8 (example), dbcf020 (AGENTS.md, STANDARDS.md step 1, audit link), and b02e0ca (review guideline).
- Review gate: `/code-review high` reported six findings, none a correctness defect in the decode. Fixed: PD3 `debugPrint()` now zero-pads the raw value to 8 hex digits; the example banner uses `F()`; the implementer's combined commit was split into one commit per register. Not changed: the example's validated PD3 read returns nothing if firmware sets a reserved bit, which is the plan's rule under "Validation tiers" and is checked on hardware below; the native harness does not compile the factory, so no factory test exists for any register (a harness gap that predates this batch); the validation preamble repeated in every class (a base-class change outside this batch). The gate also found that the PlatformIO build cache hides warnings, now recorded in CODE_REVIEW_GUIDELINES.md.
- Bench fix: the validated 42h read failed on hardware because firmware sets reserved bits 3:0 (see check 2). Per "Validation tiers", commit 97e1b48 drops the check for bits 3:0 and keeps 31:21, 16:12, 9, and 6:5 (`kCheckedReservedMask`, 0xFFE1F260). It adds tests that 0x0008001A is semantically valid and that bits 3:0 are unchecked, and records the observation in CONSTRAINTS.md under Read/Write Registers. Commit b35cd59 records the VERSION reading in its header and in AGENTS.md. After the fix: `pio test -e native` 406/406, a cache-free `pio run -e example-read-registers` compiled all 125 units with no warnings, and `scripts/verify.py` passed at b35cd59.
- Hardware, run on 2026-11-18 (Teensy 4.0, TPS25751 in APP mode with an EEPROM configuration, Boot Flags Revision ID 193; `example-read-registers` at 2889e24 plus a scratch sketch that is not in the repo):
  1. Fh [done]: raw 0xF8091166, which the class prints as F809.11.66. Under the TRM reading ("byte 1 = RR, byte 2 = MM, etc."), bytes 3 and 4 give VVVV = 0xF809, which looked implausible as a version number. On 2026-11-19 the value was compared with the board's EEPROM image from TI's Application Customization Tool (tool build 2.0.1). The tool names the image by the same word (`..._TPS25751_F8091166_fullFlash.bin`) and stores it little-endian as `66 11 09 F8` at offsets 0x834 and 0x4434, one copy per region header, so the raw value and byte order match TI's tooling. The previous image, from tool build 2.0.0 with the same questionnaire answers, holds `64 11 09 F8`, so only bits 7:0 changed between firmware releases, which fits RR in byte 1. The tool shows only the hex word, so the VVVV.MM.RR split, including VVVV = F809, remains the TRM's reading. Nothing contradicts the decode, and it is unchanged.
  2. 42h read [done]: the validated read failed. The sketch printed "PD3_CONFIGURATION: reserved bits set (0x0000000A)" and then "Failed to create/read PD3 Configuration register via factory". The unvalidated raw value is 0x0008001A: the documented reset 0x00080010 plus bits 3 and 1, in reserved range 3:0. Fixed by 97e1b48 (see Bench fix).
  3. 42h write [done]: read 0x0008001A, cleared Support Get Revision, `writePD3Config()` returned true, and the readback was 0x0000001A. XOR with the original is 0x00080000, so only bit 19 changed. Writing the original back read back 0x0008001A exactly.

Fh Version, class `TPS25751Version`, read-only, 4 bytes, p.18.

- Accessors: `rawVersion()` (bits 31:0), `versionMajor()` (VVVV, bits 31:16), `versionMinor()` (MM, bits 15:8), `versionRevision()` (RR, bits 7:0). The TRM numbers bytes from 1 and gives only "byte 1 = RR, byte 2 = MM, etc.", so the VVVV-in-bytes-3-and-4 reading is an inference to confirm on hardware. `debugPrint()` prints the value as hex `VVVV.MM.RR` because the TRM does not say whether the digits are BCD.
- Validation: no semantic field rules. 00000000h is the reset and is accepted.
- `TPS25751`: `readVersionRegister(bool validate = true)`.
- Tests (`test_TPS25751Version`): construction and Rule of Five, an all-0xFF data-tier rejection, decode of a hand-packed `{0x03, 0x02, 0x01, 0x00}` as 0001.02.03, and a reset fixture.
- Hardware: read in any state and record the value in the batch summary. Optional follow-up outside this batch: print the version after step 17 in `examples/load-patch-bundle`.

42h PD3 Configuration, class `TPS25751PD3Config`, R/W, 4 bytes, reset 00080010h, p.46.

- Accessors and `setBits()` setters for Support PPS Status (bit 20), Support Get Revision (19), Support Get Source Info (18), Support Sink Cap Extended (17), Support Battery Status Message (11), Support Battery Capabilities Message (10), Support Source Extended Message (8), Auto Get MIDB (7), and Unchunked Supported (4). Doxygen on each bit names the register it gates.
- Validation, semantic: reserved bits 31:21, 16:12, 9, and 6:5 read 0. Bits 3:0 were in this list until hardware showed firmware setting them (see Batch G's Bench fix).
- `TPS25751`: `readPD3Config()`, `writePD3Config()`, `readPD3ConfigRegister()`; `kAddress = Address::PD3_CONFIGURATION`.
- Tests (`test_TPS25751PD3Config`): the 00080010h reset decodes as Get Revision 1 and Unchunked 1 with all other bits 0, and is valid. Each setter round-trips and leaves the other bits unchanged, including preset reserved bits. A reserved bit set makes the register semantically invalid.
- Hardware: read in any state (expect 00080010h unless the EEPROM configuration differs); write Support Get Revision, read it back, and restore the original value.

Also in this batch: the Batch G items under "Per-batch documentation" (STANDARDS.md step 1, AGENTS.md roadmap link, MCP table count).

### Batch H: Global System Configuration (27h) and Received SOP Identity (48h) [done]

Both have non-standard sizes; 27h is R/W and 48h is read-only.

- Status: software complete on `fix/rev-b-trm`. The required 27h length-byte check ran on 2026-11-18 and changed the design: the controller reports 14 bytes, not 15, and the firmware sets bits in every 27h reserved range. The 27h write test and the two remaining 48h checks ran with the user at the bench on 2026-11-19 (see Hardware below), which completes the batch.
- Verified: `pio test -e native` 448/448, with new suites `test_TPS25751GlobalSystemConfig` (24 tests) and `test_TPS25751ReceivedSOPIdentity` (16 tests). After `-t clean`, `pio run -e example-read-registers` with an empty `PLATFORMIO_BUILD_CACHE_DIR` compiled all 127 units, including both new files, with no warnings. `scripts/verify.py` passed at c0c802d. `release_level.py main..fix/rev-b-trm` reports minor. The manual cross-check against MCP `get_register(GLOBAL_SYSTEM_CONFIGURATION)` and `get_register(RECEIVED_SOP_IDENTITY_DATA_OBJECT)` matched every field: 27h RCP Threshold 23:22, PP3 Config 18:16, ILIM Over Shoot 15:14, PP1 Config 10:8, PP Cable1 bit 0; 48h Number Valid VDOs 2:0, reserved 5:3 (mask 0x38), Response Type 7:6, and VDO i at bit 8 + 32i for 32 bits (39:8 through 199:168). `RegisterInfo` is 14 bytes for 27h (hardware, not the server's 116 bits) and 25 bytes for 48h (server and hardware agree). Table 3-13 (p.28) is confirmed in the rev B PDF; rev A does not document 27h. The MCP server does not serve the USB PD specification, so 48h ships raw VDOs and its typed decode waits for Batch L. Commits 540d5be (27h), a77723a (48h), 1840df9 (example), f499176 (review fix), and c0c802d (AGENTS.md, CONSTRAINTS.md, CODE_REVIEW_GUIDELINES.md).
- Mid-batch change: the bench read came in while the implementer was working, so the work order was amended before the 27h commit. `RegisterInfo` became 14 bytes, the padding-nibble handling and tests were removed, and the reserved-bit checks were dropped under "Validation tiers". The 27h bullets below record the original plan and are annotated where they changed.
- Review gate: `/code-review high` reported seven findings, none a decode error. Fixed in f499176: 27h `debugPrint()` now prints the 14 raw bytes, because the firmware's undocumented bits are otherwise invisible and every write must preserve them, and a new test covers its output. Not changed: `writeGlobalSystemConfig()` accepts a default-constructed object, which would clear PP1, PP3, and the undocumented bits (decision 2 keeps the write method; the class and method Doxygen require a fresh read); the 48h zero-padded hex loop is the third copy of that code (a shared helper is a base-class change outside this batch); the native harness still does not compile the factory, so the new factory cases are untested (the gap Batch G recorded); 27h's enums are at namespace scope while 48h nests `ResponseType` (namespace scope follows `TPS25751PortConfig` and `TPS25751AutoNegotiateSink`, and a global `ResponseType` would be too generic); `toString()` text is plain `const char*` (the existing `toString()` convention). Two patterns from the gate went into CODE_REVIEW_GUIDELINES.md under Host-Register Write-Path Traps.
- Hardware, run on 2026-11-18 (Teensy 4.0, TPS25751 in APP mode with an EEPROM configuration; a scratch raw-I2C sketch outside the repo, then `example-read-registers` at f499176):
  1. 27h read [done]: four consecutive raw reads returned length byte 14 and data `05 09 14 00 1C 03 00 00 00 00 00 00 00 00`. Board settings for Batch I: PP1 Config 1 (PP_5V is a source), PP3 Config 4 (bi-directional), ILIM Over Shoot 0 (none), RCP Threshold 0 (6 mV), PP Cable1 enabled. The firmware sets bit 2, bit 11, bit 20, and bits 34 to 36, 40, and 41, all TRM-reserved. With the 14-byte `RegisterInfo`, `readGlobalSystemConfigRegister(true)` succeeds and prints the same bytes. Recorded in CONSTRAINTS.md under Read/Write Registers.
  2. 48h, no partner [done]: run on 2026-11-19 with the board on a bench supply and the USB-C port empty (STATUS `00 00 00 00 00`, no connection, VBUS at vSafe0V). The raw read was length byte 25 and 25 zero bytes: Response Type 0 (not sent or pending), zero VDOs. `readReceivedSOPIdentityRegister(true)` succeeds and the register is semantically valid; `example-read-registers` printed the same decode with no failed reads. The no-partner reading depends on history: in Check C below, 0x48 kept the previous partner's identity after a detach until the next attach cleared it.
  3. 48h, attached source [done]: the board was a UFP sink with PORT_CONTROL Automatic ID Request = 1. The raw length byte was 25. 0x48 read Response Type 0 (not sent or pending) with zero VDOs, and INT_EVENT1 bit 51 was clear. The validated read succeeds. Per the plan, retry with a sink attached; a SOP Discover Identity normally comes from the DFP, so the board may need to be the DFP.
  4. 27h write [done]: run on 2026-11-19 with the board on a bench supply and nothing attached. A fresh validated read returned `05 09 14 00 1C 03 00 00 00 00 00 00 00 00` with length byte 14 (RCP 0, PP3 4, PP1 1, ILIM 0, Cable1 1). `setRcpThreshold(RcpThreshold::mV8)` and `writeGlobalSystemConfig()` wrote `05 09 54 …`; the read-back matched exactly, so only bits 23:22 changed and every firmware-set reserved bit survived, and the validated decode gave RCP 1 with PP3 and PP1 unchanged. Writing the original bytes back read back `05 09 14 00 1C 03 …` exactly. STATUS and INT_EVENT1 were polled every 14 ms for 1.5 s after each write, after clearing INT_EVENT1. With no partner there was nothing to disconnect: STATUS kept toggling Port Role about every 85 ms, as it did before the write, and INT_EVENT1 stayed zero. The disconnect and reconnect with a partner attached was not exercised; it would drop the partner's contract twice and is not needed to verify the write path.
  5. 48h, attached sink [done]: run on 2026-11-19 with an iPhone on the port and the board on a bench supply. First run: the board attached as Source and DFP (STATUS `7F 00 10 00 00`), then initiated a PR_Swap to Sink about 810 ms later, because PORT_CONTROL Initiate Swap to Sink is 1 in this EEPROM configuration; the iPhone accepted and sourced 5 V at 900 mA, with the board remaining DFP. 0x48 updated about 970 ms after the attach to Response Type 1 (ACK) with six VDOs: `46 AC 05 40 D5 00 00 00 00 03 31 17 75 32 00 00 65 00 00 00 00 00 00 00 43`, ID Header VID 0x05AC (Apple). INT_EVENT1 stayed zero throughout because INT_MASK1 enables only Ready for Patch and Patch Loaded (`00 … 00 03`). Second run, for the timing: with the user's agreement, INT_MASK1 bits 3 and 51 were set and PORT_CONTROL bit 5 (Initiate Swap to Sink) cleared, both verified by read-back (`08 00 00 00 00 00 08 00 00 00 03`, `52 50 81 DA`). Polling every 14 ms: attach (STATUS `7F`, plug-insert bit 3 set) at t = 12531 ms, when 0x48 also cleared to all zero; 0x48 filled with the same ACK and VDOs at 12773 ms; bit 51 set at 12879 ms, 106 ms after 0x48. The board stayed Source and DFP for the rest of the 60 s. Both registers were restored to `00 … 00 03` and `72 50 81 DA` and read back exactly. The validated read succeeds and is semantically valid in both runs. Recorded in CONSTRAINTS.md under Read/Write Registers.
- Unrelated observation: on the same run, `readTransmitSourceCapsRegister(true)` (0x32) failed validation on every loop and the example printed "Failed to create/read Transmit Source Capabilities register via factory". Batch H does not touch 0x32; the cause is not yet investigated.

27h Global System Configuration, class `TPS25751GlobalSystemConfig`, R/W, 15 bytes (116 bits) in the TRM and 14 bytes on hardware, reset 28101h, p.28.

- Accessors: `rcpThreshold()` (bits 23:22, enum 6, 8, 10, 12 mV nominal), `pp3Config()` (18:16, enum for values 0h to 5h), `ilimOvershoot()` (15:14, enum: none, 100, 200, 500 mA; Doxygen notes this is the PP_5V limit, not 28h's APDO ILIM Over Shoot), `pp1Config()` (10:8, enum for 0h and 1h), `ppCable1SwitchEnabled()` (bit 0). Each enum has `toString()` text from the TRM value tables.
- Setters: one `setBits()` setter per accessor (`setRcpThreshold()`, `setPp3Config()`, `setIlimOvershoot()`, `setPp1Config()`, `setPpCable1SwitchEnabled()`), taking the typed enums so an undefined PP1 or PP3 code cannot be written through the typed API.
- Size and padding: see "Decode technique" under Approach. Superseded by hardware: the controller's length byte is 14, so `RegisterInfo` is 14 bytes, bits 115:112 are not transmitted, and there is no padding nibble.
- Validation, semantic: PP3 Config ≤ 5 and PP1 Config ≤ 1. Reserved bits 115:24, 21:19, 13:11, and 7:1 were in this rule until hardware showed the firmware setting bits in all four ranges (see the Hardware list above).
- `TPS25751`: `readGlobalSystemConfig()`, `writeGlobalSystemConfig()`, `readGlobalSystemConfigRegister()`; `kAddress = Address::GLOBAL_SYSTEM_CONFIGURATION`. Doxygen on the class and on `writeGlobalSystemConfig()` states that every write disconnects and reconnects the port, and that PP1 and PP3 describe the board's power-switch wiring.
- Tests (`test_TPS25751GlobalSystemConfig`): the reset fixture decodes to PP3 Sink, ILIM 200 mA, PP1 Source, and Cable1 enabled, and is valid. PP3 6h and PP1 2h are each invalid. As shipped, after the hardware change: the bench value decodes and is valid, a set bit in each reserved range stays valid, and each setter round-trips from the bench value and leaves every other bit, including the firmware's undocumented bits, unchanged.
- Hardware: see the 27h row in "Hardware validation". Confirm the length byte is 15 before marking the batch done; on a mismatch, follow "Decode technique". Record the board's PP1 and PP3 settings in the batch summary for Batch I.
- CONSTRAINTS.md: the 116-bit size rule and padding nibble; the disconnect-on-write behavior and the observed reconnect on hardware.

48h Received SOP Identity, class `TPS25751ReceivedSOPIdentity`, read-only, 25 bytes, p.47.

- Accessors: `numberValidVdos()` (bits 2:0), `responseType()` (7:6, enum `NotSentOrPending`, `Ack`, `NakOrTimeout`, `Busy`), `vdo(index)` for index 0 to 5 via `extractBits32(8 + 32 * index, 32)` (returns 0 for index ≥ 6), and named aliases `idHeaderVdo()`, `certStatVdo()`, `productVdo()` for VDOs 1 to 3, as the TRM labels them. Typed decode inside the VDOs follows decision 3: in this batch if the server serves the USB PD specification when the batch starts, otherwise in Batch L.
- Validation, semantic: Number Valid VDOs ≤ 6 (TRM "Max of 6") and reserved bits 5:3 read 0. Not checked: whether VDOs past the count are zero, and whether the count is consistent with Response Type. The TRM states neither rule.
- `debugPrint()` prints only the first `numberValidVdos()` VDOs.
- `TPS25751`: `readReceivedSOPIdentityRegister()`.
- Tests (`test_TPS25751ReceivedSOPIdentity`): an ACK fixture with three VDOs at bytes 1 to 12 asserting every accessor, a fixture for each Response Type, count 7 invalid, a reserved bit invalid, and `vdo(6)` returning 0.
- Example: print 48h in the connected-state block alongside `automaticIdRequest()` and `discoverModeCompleted()`.
- Hardware: see the 48h row in "Hardware validation". If Response Type stays 0 with Automatic ID Request = 1 and a source attached, record it and retry with a sink attached. Discover Identity initiation depends on firmware policy and data role, which the TRM does not specify.
- CONSTRAINTS.md: bit 51 is a hint, not a 48h-updated signal; the observed timing.

### Batch I: TX Source Info (78h) and Transmitted PPS Status (7Ah) [done]

Both are R/W source-side info words with unit conversion. Setters invert the class's own LSB constants, per STANDARDS.md "Write-encoder pattern".

- Status: software complete on `fix/rev-b-trm`. The required length-byte checks passed before implementation started: the controller reports 8 bytes for 78h and 4 for 7Ah, as the TRM widths imply, and the firmware sets no reserved bit in either register, so every planned reserved check stays. The two 78h checks that needed a different port partner ran with the user at the bench on 2026-11-19 (see Hardware below), which completes the batch.
- Verified: `pio test -e native` 543/543, with new suites `test_TPS25751TransmitSourceInfo` (39 tests) and `test_TPS25751TransmitPPSStatus` (38 tests). After `-t clean`, `pio run -e example-read-registers` with an empty `PLATFORMIO_BUILD_CACHE_DIR` compiled all 129 units, including both new files, with no warnings. `scripts/verify.py` passed at c7ebbfe. `release_level.py main..fix/rev-b-trm` reports minor. The manual cross-check against MCP `get_register(TX_SOURCE_INFO)` and `get_register(TRANSMITTED_PPS_STATUS_DATA_BLOCK)` matched every field: 78h PortType bit 63, DPSPort 62, PortMaximumPDP_0p5W 49:41 and PortGuaranteedPDP_0p5W 40:32 at 500 mW/LSB, Port Type 31, Port Maximum, Present, and Reported PDP 23:16, 15:8, and 7:0 at 1000 mW/LSB, reserved masks 0x3FFC0000 on the upper word (61:50) and 0x7F000000 on the lower (30:24); 7Ah Output Voltage 15:0 at 20 mV/LSB with sentinel 0xFFFF, Output Current 23:16 at 50 mA/LSB with sentinel 0xFF, PTF 26:25, OMF 27, reserved mask 0xF1000000 (31:28 and 24). `RegisterInfo` is 8 bytes for 78h and 4 for 7Ah (server width and hardware length byte agree). Commits 55f02f6 (78h), 79f4870 (7Ah), 5dc149e (example), 96524f4 (review fix), and c7ebbfe (AGENTS.md, CONSTRAINTS.md).
- Review gate: `/code-review high` reported five findings, none a decode error. Fixed in 96524f4: the 7Ah `debugPrint()` printed the reserved-bit mask without zero padding (bit 24 alone read `0x1000000`); both hex lines now use one local helper, and a new test covers the bit 24 case. Not changed: the 78h rule Guaranteed PDP ≤ Maximum PDP is inferred, not stated by the TRM, on a register the firmware fills, so firmware data could fail validation; the plan asked for the rule, the board's register reads the reset (which passes), and the sourcing check below will show whether the firmware breaks it (if it does, drop the rule under "Validation tiers"); the zero-padded hex loop now has five copies across classes (a shared helper is a base-class change outside this batch); 78h reads the register as two `extractBits32()` words where `extractBits64()` exists (cosmetic); the native harness still does not compile the factory, so the six new factory cases are untested (the gap Batch G recorded). No new pattern went into CODE_REVIEW_GUIDELINES.md.
- Hardware, run on 2026-11-18 (Teensy 4.0, TPS25751 in APP mode with an EEPROM configuration, a PD charger attached and the board sinking; a scratch raw-I2C sketch outside the repo, then `example-read-registers` at c7ebbfe):
  1. Length bytes [done]: three consecutive raw reads returned 8 for 0x78 and 4 for 0x7A.
  2. 78h read, attached source [done]: `00 00 00 80 00 00 00 80`, the documented reset (both Port Type bits Guaranteed, every PDP field 0). `readTransmitSourceInfoRegister(true)` succeeds in the example.
  3. 78h upper-word write [done]: wrote the lower word as read and an upper word of PortType 1, Port Maximum PDP 30 (15 W), Port Guaranteed PDP 20 (10 W), bytes `00 00 00 80 14 3C 00 80`. The read-back matched exactly, and writing the original back read back `00 00 00 80 00 00 00 80`.
  4. 7Ah read and write [done]: read `00 00 00 00`; wrote `FA 00 3C 02` (5000 mV, 3000 mA, PTF normal, CV), read it back exactly, restored, and read back `00 00 00 00`. `readTransmitPPSStatusRegister(true)` succeeds in the example.
  5. 78h, no partner [done]: run on 2026-11-19 with the board on a bench supply and the USB-C port empty. Raw length byte 8 and `00 00 00 80 00 00 00 80`, the documented reset; `readTransmitSourceInfoRegister(true)` succeeds and the register is semantically valid.
  6. 78h, attached sink [done]: run on 2026-11-19 with an iPhone attached and the board as Source (the same runs as Batch H check 5). About 110 ms after the attach, while the board was still the source, the firmware rewrote the lower word from the reset to `64 64 00 80`: Port Reported PDP and Port Present PDP 100 W, Port Maximum PDP 0 W, Port Type Guaranteed. The upper word stayed at the reset `00 00 00 80`, and the value persisted after the iPhone was unplugged and through the second attach. `readTransmitSourceInfoRegister(true)` succeeds and the register is semantically valid. Present PDP above Maximum PDP is inconsistent with the field descriptions, but the library checks no rule on the lower word. The inferred upper-word rule, Guaranteed PDP at most Maximum PDP, is not exercised, because the firmware leaves both fields at 0, so the rule stays. Recorded in CONSTRAINTS.md under Read/Write Registers.
- Board facts for later batches: PD3_CONFIGURATION reads `0x0008001A`, so Support Get Source Info (bit 18) and Support PPS Status (bit 20) are clear and the controller answers both Get_* messages with Not_Supported on this board.

78h TX Source Info, class `TPS25751TransmitSourceInfo`, R/W, 8 bytes, reset 8000000080000000h, p.55.

- Fields: PortType (bit 63), DPSPort (62), PortMaximumPDP_0p5W (49:41, 500 mW/LSB), PortGuaranteedPDP_0p5W (40:32, 500 mW/LSB), Port Type (31), Port Maximum PDP (23:16, 1 W/LSB), Port Present PDP (15:8, 1 W/LSB), Port Reported PDP (7:0, 1 W/LSB). Raw and milliwatt accessors and setters for each. The two Port Type bits get distinct names, and each Doxygen comment cites its bit number and notes the TRM does not explain the duplication.
- Validation, semantic: reserved bits 61:50 and 30:24 read 0. DPSPort = 1 requires PortType (bit 63) = 0, stated by the TRM. PortGuaranteedPDP_0p5W ≤ PortMaximumPDP_0p5W, skipped when the maximum is 0; this rule is inferred from the TRM descriptions ("minimum power the port is guaranteed" against "maximum power the port will provide").
- `TPS25751`: read trio and `writeTransmitSourceInfo()`; `kAddress`. Doxygen quotes the TRM: "automatically updated by the PD firmware".
- Tests (`test_TPS25751TransmitSourceInfo`): reset fixture valid with both Port Type bits set; round-trip of each setter; 9-bit field masking (an over-range value does not reach bit 50 or 41); DPSPort with PortType = 1 invalid; guaranteed > maximum invalid; guaranteed > 0 with maximum 0 valid.
- Hardware: see the 78h row in "Hardware validation". The attached-sink step depends on Batch H's PP1 reading.
- CONSTRAINTS.md: firmware-updated R/W register; the observed relation between the upper and lower words.

7Ah Transmitted PPS Status, class `TPS25751TransmitPPSStatus`, R/W, 4 bytes, p.56.

- Fields: Output Voltage (15:0, 20 mV/LSB, 0xFFFF = not supported), Output Current (23:16, 50 mA/LSB, 0xFF = not supported), PTF (26:25, enum: 1h normal, 2h warning, 3h over-temperature, 0h "not defined in TRM"), OMF (27, 0 = constant voltage, 1 = current limit). Accessors: raw values, `outputVoltage_mV()`, `outputCurrent_mA()`, `outputVoltageSupported()`, `outputCurrentSupported()`. Setters: raw, unit-based, and `setOutputVoltageNotSupported()` / `setOutputCurrentNotSupported()`. Unit setters clamp to one below the sentinel, so an over-range input cannot accidentally report "not supported".
- Validation, semantic: reserved bits 31:28 and 24 read 0. PTF 0h is accepted because it is the reset.
- `TPS25751`: read trio and `writeTransmitPPSStatus()`; `kAddress`.
- Tests (`test_TPS25751TransmitPPSStatus`): decode of 5000 mV and 3000 mA from exact-multiple raw values, both sentinels, each PTF value, unit-setter clamping, setter isolation from neighbours, reserved bit invalid.
- Hardware: write and read back in any state.

### Batch J: Tx Source Caps Extended (77h) and Transmit Sink Caps Extended (7Eh) [done]

Both are byte-aligned blocks of raw fields whose encodings the TRM defers to the USB PD specification. Typed decode follows decision 3: here if the server already serves the specification, otherwise in Batch L. Accessors and setters are raw, using `setBits()` for sub-byte fields and `extractBits16`/`setBits(pos, 16, v)` for 16-bit fields (little-endian, as everywhere on the host side). Doxygen on each class states that the controller prepends VID, PID, XID, and the firmware and hardware versions.

- Status: complete on `fix/rev-b-trm`. The length-byte checks ran before implementation started: the controller reports 15 bytes for 77h and 14 for 7Eh, as the TRM widths imply, and the firmware sets no reserved bit in either register, so every planned reserved check stays. Neither the tps25751-docs server nor any other MCP server in the session serves the USB PD specification tables for Source_Capabilities_Extended or Sink_Capabilities_Extended, so both classes ship raw accessors and typed decode for 77h and 7Eh moves to Batch L.
- Verified: `pio test -e native` 593/593, with new suites `test_TPS25751TransmitSourceCapsExtended` (28 tests) and `test_TPS25751TransmitSinkCapsExtended` (22 tests). After `-t clean`, `pio run -e example-read-registers` with an empty `PLATFORMIO_BUILD_CACHE_DIR` compiled all 131 units, including both new files, with no warnings, before and after the review fixes. `scripts/verify.py` passed at e90ec7c. `release_level.py main..fix/rev-b-trm` reports minor. The manual cross-check against MCP `get_register(TX_SOURCE_CAPABILITIES_EXTENDED_DATA_BLOCK)` and `get_register(TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK)` matched every field: 77h Voltage Regulation 7:0, Hold Up Time 15:8, Compliance 23:16, Touch Current 31:24, Peak Current 1, 2, and 3 at 47:32, 63:48, and 79:64, Touch Temperature 87:80, Source Inputs 95:88, Number Fixed Batteries 99:96, Number Hot Swappable Batteries 103:100, Source PDP 110:104, reserved 119:111 (9 bits from bit 111); 7Eh SKEDB Version 7:0, Load Step 15:8, Sink Load Char 31:16, Compliance 39:32, Touch Temperature 47:40, Battery Info 55:48, Sink Modes 63:56, Sink Minimum, Operational, and Maximum PDP 71:64, 79:72, and 87:80, reserved 111:88 (24 bits from bit 88). `RegisterInfo` is 15 bytes for 77h and 14 for 7Eh (server width and hardware length byte agree). Commits 7262584 (77h), 0dc2d68 (7Eh), 9794ffe (example), 7e2e0f3 and 95b2fde (review fixes), and e90ec7c (AGENTS.md, CONSTRAINTS.md, README.md).
- Review gate: the high-effort code review reported five findings, none a decode error. Fixed in 7e2e0f3: the implementer inserted every 7Eh entry above the 77h entry in the shared lists (TPS25751.h, the factory header, its three switches and creators, TPS25751.cpp, and the native build filter); they now follow address order, which matters because Batch K inserts 7Bh and 7Dh between them. Fixed in 95b2fde: both write-method comments named the 42h gate bit without saying it must be set, and the 7Eh comments and the example's failure message said "TX Sink" where the TRM and the class say "Transmit Sink". Not changed: the native harness still does not compile the factory, so the six new factory cases are untested (the gap Batch G recorded); the zero-padded hex print helper now has seven per-class copies (a shared helper is a base-class change outside this batch). No new pattern went into CODE_REVIEW_GUIDELINES.md.
- Hardware, run on 2026-11-18 (Teensy 4.0, TPS25751 in APP mode with an EEPROM configuration, a PD charger attached and the board sinking; a scratch raw-I2C sketch outside the repo, then `example-read-registers` at 95b2fde):
  1. Length bytes [done]: three consecutive raw reads returned 15 for 0x77 and 14 for 0x7E.
  2. 77h read [done]: `00 00 00 00 00 00 00 00 00 00 00 00 00 7F 00`. Source PDP is 0x7F (127), filled from the firmware configuration, not the documented reset of 0; no reserved bit is set. `readTransmitSourceCapsExtendedRegister(true)` succeeds in the example and decodes Source PDP 127 with every other field 0.
  3. 77h write [done]: wrote `01 02 03 04 05 06 07 08 09 0A 0B 06 21 41 00` (Source Inputs 0x06 with bit 0 clear, one fixed and two hot-swappable batteries, Source PDP 0x41). Four reads 250 ms apart matched exactly, and writing the original back read back exactly. Source Inputs bit 0 did not change on its own, in the test value or in the original over the following reads; this board has no Barrel_Jack_Event.
  4. 7Eh read and write [done]: read all zero (the documented reset); wrote `01 02 03 04 05 06 07 08 09 0A 0B 00 00 00`, read it back exactly four times, restored, and read back all zero. `readTransmitSinkCapsExtendedRegister(true)` succeeds in the example.
  5. Replies [unit-test only, as planned]: PD3_CONFIGURATION reads `0x0008001A`, so Support Source Extended Message (bit 8) and Support Sink Cap Extended (bit 17) are clear and the controller answers Get_Source_Cap_Extended and Get_Sink_Cap_Extended with Not_Supported on this board.

77h Tx Source Caps Extended, class `TPS25751TransmitSourceCapsExtended`, R/W, 15 bytes, p.54.

- Fields by byte: Voltage Regulation (byte 0), Hold Up Time (1), Compliance (2), Touch Current (3), Peak Current 1, 2, 3 (bytes 4-5, 6-7, 8-9, 16-bit), Touch Temperature (10), Source Inputs (11), Number Fixed Batteries (byte 12 bits 3:0), Number Hot Swappable Batteries (byte 12 bits 7:4), Source PDP (byte 13 bits 6:0), reserved (byte 13 bit 7 and byte 14). Peak currents use an indexed `peakCurrent(1..3)` accessor.
- Validation, semantic: reserved bits 119:111 read 0; each battery count ≤ 4 (TRM "Max of 4").
- Doxygen: Source Inputs bit 0 can be changed by the Barrel_Jack_Event GPIO event, so a read-back can differ from the value written.
- `TPS25751`: read trio and `writeTransmitSourceCapsExtended()`; `kAddress`.
- Tests (`test_TPS25751TransmitSourceCapsExtended`): a hand-packed fixture asserting every accessor, 16-bit little-endian peak currents, nibble isolation in byte 12, Source PDP masking to 7 bits (bit 111 untouched), battery count 5 invalid, reserved bit invalid, setter round-trips.

7Eh Transmit Sink Caps Extended, class `TPS25751TransmitSinkCapsExtended`, R/W, 14 bytes, p.59.

- Fields by byte: SKEDB Version (0), Load Step (1), Sink Load Char (bytes 2-3, 16-bit), Compliance (4), Touch Temperature (5; TI "Touch Temprature"), Battery Info (6), Sink Modes (7), Sink Minimum PDP (8), Sink Operational PDP (9), Sink Maximum PDP (10), reserved (bytes 11 to 13).
- Validation, semantic: reserved bits 111:88 read 0. There is no PDP ordering check: the TRM gives no encoding for the PDP bytes, so a numeric comparison of raw bytes could reject a valid value.
- `TPS25751`: read trio and `writeTransmitSinkCapsExtended()`; `kAddress`.
- Tests (`test_TPS25751TransmitSinkCapsExtended`): a hand-packed fixture asserting every accessor, 16-bit Sink Load Char, reserved bit invalid, setter round-trips with neighbours preserved.

Hardware for both: write and read back in any state. Replies to Get_Source_Cap_Extended and Get_Sink_Cap_Extended are unit-test only.

### Batch K: BSDO (7Bh) and Tx Battery Capabilities (7Dh) [done]

Apply v01's "Batch-D Tightening" by analogy: the orchestrator writes the full byte map for both registers into the work order, and the review gate re-derives battery 3's 7Dh offsets by hand. The implementer runs at high effort and the gate at `/code-review xhigh` (see "Models and effort").

- Status: complete on `fix/rev-b-trm`; the library decodes 37 of 37 registers. The length-byte checks ran before the work order was written: the controller reports 16 bytes for 7Bh and 36 for 7Dh, as the TRM widths imply, and the firmware sets no 7Bh reserved byte, so every planned reserved check stays. No MCP server in the session serves the USB PD specification tables for BSDO or Battery_Capabilities, so both classes ship raw accessors and typed decode for 7Bh and 7Dh moves to Batch L. The work order carried the full byte map for both registers. The base class is unchanged: 7Dh uses class-private byte helpers.
- Verified: `pio test -e native` 652/652, with new suites `test_TPS25751TransmitBatteryStatus` (25 tests), `test_TPS25751TransmitBatteryCapabilities` (28 tests), and `test_TPS25751RegisterAddress` (6 tests, from the bench fix below). After `-t clean`, `pio run -e example-read-registers` with an empty `PLATFORMIO_BUILD_CACHE_DIR` compiled all 133 units, including both new files, with no warnings, before and after the review and bench fixes. `scripts/verify.py` passed at 706953b. `release_level.py main..fix/rev-b-trm` reports minor. The manual cross-check against MCP `get_register(TRANSMITTED_BATTERY_STATUS_DATA_OBJECTS_(BSDO))` and `get_register(TX_BATTERY_CAPABILITIES)` matched every field. 7Bh: Battery Info i at bit 32i + 8 (15:8, 47:40, 79:72, 111:104), Present Capacity i at bit 32i + 16 (31:16, 63:48, 95:80, 127:112), reserved bytes 0, 4, 8, and 12 (7:0, 39:32, 71:64, 103:96). 7Dh: entry i at byte 9i, with VID, PID, Design Capacity, and Last Full Charge Capacity at +0, +2, +4, and +6 and Battery Type at +8. Battery 3 was re-derived by hand: `entryOffset(3)` is 27, so the fields start at bytes 27, 29, 31, 33, and 35, which are bits 216, 232, 248, 264, and 280, matching the server's VID 3 231:216, PID 3 247:232, Design Capacity 263:248, Last Full Charge Capacity 279:264, and Battery Type 287:280. The suite's byte k = 0x40 + k fixture expects VID 3 0x5C5B, PID 3 0x5E5D, Design 0x605F, Last Full 0x6261, and Type 0x63, which agree with bytes 27 to 35. `RegisterInfo` is 16 bytes for 7Bh and 36 for 7Dh (server width and hardware length byte agree). Commits a886a54 (7Bh), 74bd5f3 (7Dh), bcecb5e (example), 5285aee (review fix), 80f6928 (bench fix), and 706953b (AGENTS.md, CONSTRAINTS.md, CODE_REVIEW_GUIDELINES.md, README.md).
- Review gate: `/code-review xhigh` reported six findings, none a decode error. Fixed in 5285aee: 7Bh wrote its bit offsets as the literals `32 * battery + 8` and `+ 16` while validation used `kEntrySize`; two private helpers now derive both from `kEntrySize`. Not changed: the example re-reads 42h and 77h in `print_battery_data()` (each print function stays self-contained, as in `print_caps_extended()`); the zero-padded hex print helper now has nine per-class copies (a shared helper is a base-class change outside this batch); the 7Dh byte helpers duplicate what a `uint16_t` base-class `bitPos` would provide (a base-class API change that needs an ADR, recorded in CONSTRAINTS.md); the shared lists in `TPS25751.h` and the factory follow batch order (0x78, 0x7A, 0x77, 0x7B, 0x7D, 0x7E, 0x48), not address order, so the Batch J note above that they "now follow address order" holds only for 77h before 7Eh; an all-0xFF 7Dh fails the data tier, the house bus-error convention for every register. The implementer inserted the new entries after 0x77 and before 0x7E everywhere, as ordered.
- Bench fix: the walk over all 37 addresses returned null for CUSTOMER_USE (0x06). `getRegisterInfo()` had no CUSTOMER_USE case, only `getRegisterSize()` did, so `readRegisterByAddress()` returned null and `writeCustomerUse()` returned false without writing, since the first commit of the address table (41f5dbb). Commit 80f6928 adds the case and the `test_TPS25751RegisterAddress` suite, which checks both switches for every register and fails two of six tests without the fix. Two patterns went into CODE_REVIEW_GUIDELINES.md under Host-Register Write-Path Traps.
- Hardware, run on 2026-11-19 (Teensy 4.0, TPS25751 in APP mode with an EEPROM configuration, a PD charger attached and the board sinking; a scratch raw-I2C sketch and a scratch sketch built on the library, both outside the repo, then `example-read-registers` at 80f6928):
  1. Length bytes [done]: three consecutive raw reads returned 16 for 0x7B and 36 for 0x7D.
  2. 7Bh read [done]: `00 02 FF FF` followed by twelve zero bytes, the documented reset; no reserved byte set. `readTransmitBatteryStatusRegister(true)` succeeds, so the reset passes the data tier.
  3. 7Bh write [done]: a raw write of four distinct entries read back exactly four times 250 ms apart, and the restore read back exactly. Through the class, `setBatteryInfo(3, 0x44)` and `setRawPresentCapacity(3, 0xDEF0)` with `writeTransmitBatteryStatus()` read back exactly and validated, and the restore matched.
  4. 7Dh write [done]: a raw write of byte k = 0x10 + k to all 36 bytes read back exactly and restored exactly. A raw write that changed only bytes 27 to 35 left bytes 0 to 26 unchanged. Through the class, with byte 26 (battery 2 Battery Type) first set to 0x5A, every battery 3 setter plus `writeTransmitBatteryCapabilities()` read back `A1 B2 C3 D4 E5 F6 07 18 29` at bytes 27 to 35 with bytes 0 to 26, including 0x5A at byte 26, unchanged; the restore read back all zero.
  5. 77h agreement [done, inconclusive by design]: 0x77 reads zero fixed and zero hot-swappable batteries while 7Bh battery 0 holds the reset value and 7Dh is all zero. Whether the 7Bh reset entry counts as populated depends on the Battery Info encoding, a USB PD specification structure (Batch L).
  6. 37 addresses [done]: after 80f6928, `readRegisterByAddress()` returns an object for all 37 addresses and every one passes `hasValidData()` and `isSemanticallyValid()`.
  7. Replies [unit-test only, as planned]: PD3_CONFIGURATION reads `0x0008001A`, so Support Battery Status Message (bit 11) and Support Battery Capabilities Message (bit 10) are clear and the controller answers Get_Battery_Status and Get_Battery_Capabilities with Not_Supported on this board.
- Found on the bench, outside this batch: CUSTOMER_USE (0x06) is `[R]` in rev B ("can only be changed during application customization, not at runtime", p.15). After 80f6928, `writeCustomerUse()` reaches the bus and the controller ACKs it, but the read-back keeps the old value. Recorded in CONSTRAINTS.md. The user decided on 2026-11-19 to remove it: a follow-up `fix(registers)!` commit removes `writeCustomerUse()`, the class setters, and `kAddress`, which makes the release major (see "Release level").

7Bh BSDO, class `TPS25751TransmitBatteryStatus`, R/W, 16 bytes, reset FFFF0200h in the low word, p.57.

- Layout: four 4-byte entries at byte 4i: reserved byte, Battery Info (byte 4i+1), Present Capacity (bytes 4i+2 and 4i+3, 16-bit). Indexed accessors and setters `batteryInfo(i)`, `rawPresentCapacity(i)` for i 0 to 3, returning 0 or doing nothing for i ≥ 4. Battery 3's capacity uses the same name as the others; its Doxygen quotes TI's "Battery 3 Present Info".
- Validation, semantic: reserved bytes 0, 4, 8, and 12 read 0. The reset fixture (Present Capacity FFFFh, Battery Info 2h on battery 0) must be valid; the test confirms the whole-buffer all-0xFF check does not trip on it.
- `TPS25751`: read trio and `writeTransmitBatteryStatus()`; `kAddress`.
- Tests (`test_TPS25751TransmitBatteryStatus`): reset fixture, per-entry round-trip with the other entries unchanged, out-of-range index, reserved bit invalid.

7Dh Tx Battery Capabilities, class `TPS25751TransmitBatteryCapabilities`, R/W, 36 bytes, p.58.

- Layout: four 9-byte entries at byte 9i: VID (+0, 16-bit), PID (+2, 16-bit), Battery Design Capacity (+4, 16-bit), Battery Last Full Charge Capacity (+6, 16-bit), Battery Type (+8). Battery 3 is bytes 27 to 35 (bits 287:216). The library treats it as battery index 3 despite the TRM's "battery index 0" text, because that is the only reading consistent with the field names (VID 3, PID 3) and the 72-bit stride.
- Decode and encode: byte assembly for all entries, per "Decode technique".
- Validation: the register has no reserved bits and the TRM states no rule, so the semantic tier is the base checks only.
- `TPS25751`: read trio and `writeTransmitBatteryCapabilities()`; `kAddress`.
- Tests (`test_TPS25751TransmitBatteryCapabilities`): a fixture with distinct values in every field of every entry, asserting all 20 accessors; battery 3 decode at bytes 27 to 35; setters on battery 3 leave battery 2's last byte (byte 26) unchanged; out-of-range index; bounds guard on a short buffer.
- CONSTRAINTS.md: extend the IO_CONFIG note ("Any future register whose highest bit offset exceeds 255 ...") with the first R/W case and the class-local write helpers. Record the TRM's battery-index labeling error.
- Doxygen on 77h, 7Bh, and 7Dh: keep 77h's battery counts consistent with the populated entries.

Hardware for both: see the 7Bh and 7Dh rows in "Hardware validation".

### Batch L: Typed decode from the USB PD specification [in progress]

Runs because Batches H, J, and K shipped raw accessors before an MCP server served the USB PD specification: Batch H shipped 48h with raw VDOs, Batch J shipped 77h and 7Eh with raw fields, and Batch K shipped 7Bh and 7Dh with raw entries. It adds typed decode to those five classes, plus the 7Ah PTF meaning. Every addition is a new accessor beside the raw ones; no existing accessor, setter, or validation rule changes, except the 7Ah PTF text below, which the user approved on 2026-11-19. Batch L itself is additive and minor. The release is still major (3.0.0), because the branch carries the earlier breaking removal of `writeCustomerUse()` (see "Release level").

- Branch: `fix/rev-b-trm`. The implementer works in an isolated worktree based on its tip, and its commits come onto `fix/rev-b-trm` by fast-forward or cherry-pick. The whole branch ships as one release from one human push (decision 4).
- Specification source: the `usb-pd-docs` MCP server (tools `mcp__usb-pd-docs__*`, the same engine as `tps25751-docs`), backed by `specifications/USB_PD/specification.json` in the sibling `device-register-mcp-server` repo and added to this repo's `.mcp.json` in adc507b. It serves USB PD R3.3 V1.0 (Rev. 3.3). The work order pastes each table from the server; no field encoding comes from memory.
- Implementer `sonnet` at `high` effort; gate `/code-review high` plus a hand re-derivation of one decoded value per specification table (see "Models and effort").

Coverage check, run on 2026-11-19 before the work order:

| Register | Specification structure (R3.3) | Table, page | TRM byte to specification byte | Result |
|---|---|---|---|---|
| 48h VDO 0 | ID Header VDO | 6.34, p.159 | VDO bits are specification bits | Covered. 48h is the SOP response, so the SOP (UFP) and SOP (DFP) Product Type sets apply. |
| 48h VDO 1 | Cert Stat VDO | 6.38, p.162 | Same | Covered: XID 31:0. |
| 48h VDO 2 | Product VDO | 6.39, p.162 | Same | Covered: USB Product ID 31:16, bcdDevice 15:0. |
| 77h | Source_Capabilities_Extended Data Block (SCEDB) | 6.59, p.185 | TRM byte n is SCEDB byte n + 10, for n 0 to 13 | Covered. The controller prepends SCEDB bytes 0 to 9 (VID, PID, XID, FW and HW versions). The EPR Source PDP Rating (SCEDB byte 24) has no TRM field. |
| 7Eh | Sink_Capabilities_Extended Data Block (SKEDB) | 6.70, p.199 | TRM byte n is SKEDB byte n + 10, for n 0 to 10 | Covered. The three EPR Sink PDP bytes (SKEDB 21 to 23) fall in TRM reserved bits 111:88. |
| 7Bh | Battery Status Data Object (BSDO) | 6.24, p.146 | Entry i bits are BSDO bits | Covered. |
| 7Dh | Battery Capability Data Block (BCDB) | 6.64, p.194 | Entry i byte k is BCDB byte k | Covered. |
| 7Ah | PPS Status Data Block (PPSSDB) | 6.67, p.197 | Same | Covered. PTF 00b is "Not Supported". |

Notes from the check. The TRM gives 77h Source PDP as 7 bits (110:104); the specification field is a full byte with valid values 0 to 100 W, so every code with bit 7 set is invalid anyway, and the TRM width and the 111 reserved check stay. The hardware value 0x7F (127) decodes as invalid. The specification gives the Duty Cycle sub-field of Peak Current and Sink Load Characteristics as 4 bits with values "0..31"; only 0 to 15 fit, and the decode uses the 4 bits at 5% per LSB. The Product Type VDOs that can follow in 48h VDOs 3 to 5 (UFP VDO, DFP VDO; tables 6.40 to 6.44) are also on the server and are out of scope.

Accessors, all decode-only (the raw setters stay the write path):

- 48h `TPS25751ReceivedSOPIdentity`. ID Header (VDO 0): `usbHostCapable()` (31), `usbDeviceCapable()` (30), `productTypeUfp()` and `rawProductTypeUfp()` (29:27, enum `ProductTypeUfp`), `alternateModesSupported()` (26), `productTypeDfp()` and `rawProductTypeDfp()` (25:23, enum `ProductTypeDfp`), `connectorType()` and `rawConnectorType()` (22:21, enum `ConnectorType`), `usbVendorId()` (15:0). Cert Stat (VDO 1): `xid()`. Product (VDO 2): `usbProductId()` (31:16), `bcdDevice()` (15:0). The values mean something only when `responseType()` is `Ack` and `numberValidVdos()` covers the VDO.
- 77h `TPS25751TransmitSourceCapsExtended`: `loadStep()` (Voltage Regulation 1:0, enum `LoadStep`), `iocPercent()` (bit 2: 25 or 90), `holdUpTimeSupported()` and `holdUpTime_ms()`, `lpsCompliant()`, `ps1Compliant()`, `ps2Compliant()`, `lowTouchCurrentEps()`, `groundPinSupported()`, `groundPinProtectiveEarth()`, per Peak Current i (1 to 3) `peakCurrentOverloadPercent(i)` (4:0, 10%/LSB, 26 to 31 treated as 25), `peakCurrentOverloadPeriod_ms(i)` (10:5, 20 ms/LSB), `peakCurrentDutyCyclePercent(i)` (14:11, 5%/LSB), `peakCurrentVbusDroop(i)` (15); `touchTemperatureStandard()` (enum `TouchTemperature`: IEC 60950-1, IEC 62368-1 TS1, TS2), `externalSupplyPresent()`, `externalSupplyUnconstrained()`, `internalBatteryPresent()` (Source Inputs bits 0 to 2), `sourcePdpValid()` and `sourcePdp_W()`.
- 7Eh `TPS25751TransmitSinkCapsExtended`: `skedbVersionValid()` (1 is Version 1.0), `loadStep()` (enum `LoadStep`), `sinkLoadOverloadPercent()`, `sinkLoadOverloadPeriod_ms()`, `sinkLoadDutyCyclePercent()`, `sinkLoadVbusDroopTolerated()`, the three compliance flags, `touchTemperatureStandard()` (enum `TouchTemperature`: none, IEC 60950-1, TS1, TS2; a different encoding from 77h), `fixedBatteryCount()` and `hotSwappableBatterySlotCount()` (Battery Info nibbles), the seven Sink Modes flags, and `valid`/`_W` pairs for Sink Minimum, Operational, and Maximum PDP.
- 7Bh `TPS25751TransmitBatteryStatus`, per battery i: `invalidBatteryReference(i)`, `batteryPresent(i)`, `chargingStatus(i)` (enum `ChargingStatus`), `presentCapacityKnown(i)`, `presentCapacity_mWh(i)` (0.1 Wh/LSB, FFFFh unknown).
- 7Dh `TPS25751TransmitBatteryCapabilities`, per battery i: `invalidBatteryReference(i)` (Battery Type bit 0), `batteryPresent(i)` (Design Capacity not 0000h), `designCapacityKnown(i)`, `batteryDesignCapacity_mWh(i)`, `lastFullChargeCapacityKnown(i)`, `batteryLastFullChargeCapacity_mWh(i)` (0.1 Wh/LSB; 0000h not present, FFFFh unknown).
- 7Ah `TPS25751TransmitPPSStatus`: `PresentTemperatureFlag::NotSupported = 0`, with `NotDefined` kept as a deprecated alias of the same value; `toString()` returns "Not supported" for 00b. The user approved the string change on 2026-11-19.
- Every new enum gets a `toString()`; codes the specification marks invalid print "Invalid".
- `debugPrint()` adds the typed decode below the existing lines, which stay as they are. `examples/read-registers` prints the typed decode where it prints the raw fields.
- Tests: extend each existing suite (48h, 77h, 7Eh, 7Bh, 7Dh, 7Ah) with one fixture per decoded structure, packing the bytes from the pasted tables and commenting the arithmetic.
- Hardware: re-run the 48h attached-partner check and compare the typed decode with the partner's known identity (an iPhone: VID 05ACh). Read 77h, 7Bh, and 7Dh and check that the typed decode of their current contents is sensible. The rest is unit-test only, because PD3_CONFIGURATION (0x0008001A) gates the replies off on this board.

## Files touched

| File | Batches |
|---|---|
| `include/TPS25751RegisterAddress.h` | G, H, I, J, K |
| `include/TPS25751RegisterFactory.h`, `src/TPS25751RegisterFactory.cpp` | G, H, I, J, K |
| `include/TPS25751.h`, `src/TPS25751.cpp` | G, H, I, J, K |
| `platformio.ini` (`[env:native]` `build_src_filter`) | G, H, I, J, K |
| `include/TPS25751Version.h`, `src/TPS25751Version.cpp` (new) | G |
| `include/TPS25751PD3Config.h`, `src/TPS25751PD3Config.cpp` (new) | G |
| `include/TPS25751GlobalSystemConfig.h`, `src/TPS25751GlobalSystemConfig.cpp` (new) | H |
| `include/TPS25751ReceivedSOPIdentity.h`, `src/TPS25751ReceivedSOPIdentity.cpp` (new) | H |
| `include/TPS25751TransmitSourceInfo.h`, `src/TPS25751TransmitSourceInfo.cpp` (new) | I |
| `include/TPS25751TransmitPPSStatus.h`, `src/TPS25751TransmitPPSStatus.cpp` (new) | I |
| `include/TPS25751TransmitSourceCapsExtended.h`, `src/TPS25751TransmitSourceCapsExtended.cpp` (new) | J |
| `include/TPS25751TransmitSinkCapsExtended.h`, `src/TPS25751TransmitSinkCapsExtended.cpp` (new) | J |
| `include/TPS25751TransmitBatteryStatus.h`, `src/TPS25751TransmitBatteryStatus.cpp` (new) | K |
| `include/TPS25751TransmitBatteryCapabilities.h`, `src/TPS25751TransmitBatteryCapabilities.cpp` (new) | K |
| `test/test_native/test_TPS25751<Class>/` (one new suite per class) | G, H, I, J, K; L extends the H, I (7Ah only), J, and K suites |
| 48h, 77h, 7Eh, 7Bh, 7Dh, and 7Ah class files above | L |
| `examples/read-registers/src/main.cpp`, `examples/README.md`, `examples/AGENTS.md` | G, H, I, J, K |
| `AGENTS.md` | G, H, I, J, K |
| `docs/plans/trm-revB-audit-remaining-registers.md` | G, H, I, J, K, L |
| `docs/plans/trm-revB-audit.md` | G |
| `docs/engineering/STANDARDS.md` | G |
| `docs/engineering/CONSTRAINTS.md` | H, I, K |
| `docs/engineering/CODE_REVIEW_GUIDELINES.md` | Any batch whose gate finds a new pattern |
| `README.md` | J or K |

## Verification

Per batch, from the library root on `fix/rev-b-trm`:

1. Native tests are green, including the new suite:
   ```bash
   pio test -e native
   ```
2. The example builds without warnings:
   ```bash
   pio run -e example-read-registers
   ```
3. The workflow gate passes on a rebased, clean tree (it reruns both of the above plus commit linting):
   ```bash
   scripts/verify.py
   ```
4. Every new field's bit offset and width matches `get_register` for that register, checked by hand in the review gate, and the class's `RegisterInfo` size matches `list_registers` (after Batch K, 37 of 37).
5. The release level for the branch is `minor`:
   ```bash
   python3 scripts/release_level.py main..fix/rev-b-trm
   ```
6. The hardware check in the batch's "Changes" entry is done or explicitly recorded as not possible, with the observed values in the batch summary. Batch H is not marked done until the 27h length byte has been read on hardware.

After Batch K, `readRegisterByAddress()` returns a non-null object for all 37 addresses on hardware, and the AGENTS.md counts read 37/37.
