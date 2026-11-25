---
name: tps25751-pd-verify
description: Verify the TPS25751 library's register decode against a USB PD negotiation that a ChargerLAB KM003C meter captured on the wire. Use when asked to check, verify, or cross-check TPS25751 registers (0x30 to 0x35, 0x48, 0x6A) against real PD traffic, to capture a negotiation on the bench, or to save a capture fixture bundle. Needs the km003c and tps25751-bench MCP servers and a board running the pd-bench console.
---

# Verify TPS25751 register decode against the wire

This skill starts a USB PD negotiation on a bench board, captures it with the KM003C
meter, takes a register snapshot through the bench console, and compares every word the
wire carried with the register bits that should hold it. The result is one table with
raw hex in every row and a verdict.

Tools are named as server plus tool: `km003c` `pd_capture_start` is the tool
`pd_capture_start` of the MCP server `km003c`. In Claude Code that tool is
`mcp__km003c__pd_capture_start`; in Codex it is the `pd_capture_start` tool of the
`km003c` server. Servers used:

| Server | Tools this skill calls | Touches hardware |
|---|---|---|
| `km003c` | `device_info`, `measure`, `pd_capture_start`, `pd_capture_mark`, `pd_capture_wait`, `pd_capture_stop`, `pd_capture_read` | `device_info`, `measure`, `pd_capture_start`, and `pd_capture_stop` claim the meter. `pd_capture_mark`, `pd_capture_wait`, and `pd_capture_read` hold no claim; they work on the running capture or a file |
| `tps25751-bench` | `bench_hello`, `bench_snapshot`, `bench_read`, `bench_renegotiate`, `bench_crosscheck` | All but `bench_crosscheck`, which reads two files |
| `usb-pd-docs` | `get_register`, `explain_bitfield` | No |

If `km003c` or `tps25751-bench` is not available, stop and say which server is missing.
`bench_crosscheck` is newer than the other bench tools; when it is absent, apply the
rules in [compare-rules.md](compare-rules.md) by hand, which give the same rows.

## Rules that hold throughout

- Never call a `km003c` tool whose name starts with `trigger_`. The meter would take part
  in the negotiation it is meant to observe, and it can raise VBUS on its output.
- Never call `tps25751-bench` `bench_write` or `bench_task` in this skill. Every stimulus
  goes through `bench_renegotiate`, which checks the power role and restores what it
  changes. When a result names a `bench_write` call as the recovery, give it to the user;
  do not run it.
- Before any stimulus that drops VBUS (`port_reset`, `restart`, `restart_cold`), tell the
  user in one sentence that VBUS will drop to vSafe0V and anything powered from VBUS
  will lose power, then wait for their go-ahead. If the user says the board running the
  console is powered from VBUS, offer only `gsrc`, `aneg`, and `ssrc`.
- Start the capture before the stimulus, always. The meter records only what happens
  after `pd_capture_start` returns.
- Read only `summary`, `facts`, and the narrow `messages` reads of Step 4 in the main
  session. Deeper reads follow the Large captures section.
- Every value you report sits beside the hex it came from.

## Arguments

| Argument | Values | Default |
|---|---|---|
| `stimulus` | `gsrc`, `aneg`, `ssrc`, `port_reset`, `restart`, `restart_cold`, `human` | `gsrc` when the board is sink, `ssrc` when it is source |
| `stop` | `contract`, `identity`, or a message count | `contract` |
| `save_fixture` | a bundle name, or absent | absent |

Which stimulus fits the board, from `bench_hello`:

| Stimulus | Power role | Also needs | Drops VBUS | Sends Source_Capabilities | Ends with an attach (Discover Identity can follow) |
|---|---|---|---|---|---|
| `gsrc` | sink | `plug` true, `mode` APP | No | Yes | No |
| `aneg` | sink | `plug` true, `mode` APP | No; steps between PDO voltages | No | No |
| `ssrc` | source | `plug` true, `mode` APP | No | Yes, from the board | No |
| `port_reset` | either | `mode` APP | Yes | Yes | Yes |
| `restart` | either | none | Yes | Yes | Yes |
| `restart_cold` | either | an EEPROM (`patch_source` eeprom) or a patch bundle (`patch_bundle` true) | Yes | Yes | Yes |
| `human` | either | a person at the bench | Yes, by unplugging | Yes | Yes |

Refuse a stimulus that does not fit, name the one that does, and stop. `bench_renegotiate`
refuses the same cases; do not rely on that alone.

`stop` `identity` needs a stimulus that ends with an attach: only then does a Discover
Identity exchange follow the contract. With `gsrc`, `aneg`, or `ssrc` and `stop`
`identity`, tell the user that this stimulus starts no Discover Identity, so 0x48 cannot
be compared against the wire, and offer `port_reset` (drops VBUS) or `human` instead. If
the user keeps the stimulus, run it with `stop` `contract` and say in the report that the
identity comparison was not attempted.

## Step 1: Preflight

1. Call `km003c` `device_info`. An error result with kind `DeviceBusyError` names the
   process holding the meter: report it and stop. Record `fw_version`.
2. Call `tps25751-bench` `bench_hello`. On an error, report it and stop. Record `role`,
   `plug`, `mode`, `lib`, `version`, `patch_pending`, and `patch_source`.
3. If `mode` is PTCH and `patch_pending` is true, do not take a snapshot: clearing
   INT_EVENT1 in PTCH removes Ready for Patch and the next patch load writes nothing.
   The recovery is `restart_cold`; offer it (it drops VBUS) and stop until the user
   answers. If `mode` is anything else but APP, report it and stop.
4. Check the stimulus and `stop` against the Arguments section.
5. Call `km003c` `measure` once and `tps25751-bench` `bench_snapshot` once, without
   `path`. When STATUS (0x1A) bit 0 reads 1 (a plug is present), the meter's `vbus_v`
   times 1000 and 0x6A byte 3 times 98 mV must differ by at most 98 mV plus 1% of the
   meter reading. Otherwise stop and report both values with the 0x6A hex: the meter is
   probably not between the source and this board. With no plug, skip the check; 0x6A is
   not updated in Unconnected Sleep.

## Step 2: Start the capture

For `human`, first give the user the steps in Step 3 and complete the unplug; then
start the capture here.

Call `km003c` `pd_capture_start` with:

- `label`: `pd-verify <stimulus>`.
- `provenance`: an object with `stimulus`, `stop`, the board `role`, and the
  `bench_hello` fields `lib` and `version`. `bench_crosscheck` reads `stimulus` from it.
- `stop_on`: `none`. The meter's own `contract` stop would end the capture on the first
  of several negotiations (`aneg` runs two, and a restart can attach twice about 1.9 s
  apart), and its `identity` stop fires on a Discover Identity ACK from either port,
  including the board's own. Step 4 ends the capture on evidence instead.
- `stop_after_messages`: the count, when `stop` is one; otherwise leave it out.
- `timeout_s`: 60 for `gsrc`, `aneg`, and `ssrc`; 120 for `port_reset`, `restart`, and
  `restart_cold`; for `human`, 30 with `stop` `contract` (as the quoted step 4 says) and
  45 with `stop` `identity`, which you tell the user before the steps.

If the result is an error (for example `PdMonitorNotEnabledError` or `DeviceBusyError`),
run no stimulus: report the error and its kind and stop. If the result's `pd_monitor`
reports a refusal, carry on: on meter firmware 2.0.0 the capture works without the
monitor enabled. Record the absolute `path` the result returns.

## Step 3: Stimulus

For every stimulus except `human`:

1. Call `km003c` `pd_capture_mark` with `stimulus <method> sent`.
2. Call `tps25751-bench` `bench_renegotiate` with `method` set to the stimulus.
3. Call `km003c` `pd_capture_mark` with `stimulus <method> returned`.

Then check the result before going on:

- `error` present: call `km003c` `pd_capture_stop`, report `error`, `kind`, and
  `reply_lines`, and stop.
- For `aneg`, `restored_0x37` false: 0x37 still holds the temporary value. Call
  `km003c` `pd_capture_stop`, give the user `restore_error` word for word (it names the
  recovery), and stop. Take no snapshot and make no comparison.
- For `aneg`, `second_aneg_error` present: 0x37 is restored but the contract may still be
  the temporary one. Call `km003c` `pd_capture_stop`, give the user `second_aneg_error`
  word for word, and stop without a snapshot or comparison.
- For `aneg`, record `stepped_down`, `restored_contract`, and `cleared_bit6`.
  `stepped_down` false means the controller kept its contract and the capture holds no
  step-down Request; `restored_contract` false means the final contract is not the one
  before the call. Report both.

For `human`, the cold-attach steps depend on the board's role.

Board as sink. Before starting the capture in step 2, give the user these steps from
Phase 1 of the toolset plan word for word:

> Cabling for every attach: charger into the meter's receptacle (input), meter's plug into
> the TPS25751 board (sink). The meter's own host USB cable stays connected throughout.
> The board stays on its battery and is never switched off.
>
> 1. When I say **"unplug"**, unplug the meter from both the charger and the board, and
>    reply "done".
> 2. I start the capture and say **"attach now"**.
> 3. Plug the meter into the board first, then plug the charger into the meter. Reply
>    "done".
> 4. I stop the capture after PS_RDY plus 5 s, or at 30 s.

Then say "unplug" and wait for "done". Start the capture (step 2), call
`pd_capture_mark` with `human attach now`, say "attach now", and wait for "done".

Board as source. The cabling is the board into the meter's receptacle (input) and the
meter's plug into the partner, for example an iPhone. Give the same four steps with
"charger" read as the board and "board" read as the partner, and say that trigger tools
stay off while anything is on the meter's plug.

## Step 4: Wait for the evidence, then stop the capture

The capture runs until you stop it, its message count is reached, or its timeout. Wait
in steps, each one call to `km003c` `pd_capture_wait` with `timeout_s` 2, which returns
the summary after 2 s with `running` true, or earlier with `running` false once the
capture has stopped on its own. Start right after Step 3 (for `human`, after the user's
"done"). In each summary:

- P is `contract.t_ps_rdy`, the PS_RDY of the latest completed contract, or none.
- A step is quiet when P is the same as in the previous step and no `disconnect` line
  with a time after P has appeared.

Stop waiting when one of these holds:

1. `running` is false. The capture ended by itself (message count or timeout).
2. `stop` is `contract` or a count: P is set and the last 3 steps were quiet, which puts
   the end at least 6 s after the final PS_RDY. A restart can attach a second time about
   1.9 s after the first, and the Phase 1 cold-attach steps end at PS_RDY plus 5 s, so a
   shorter wait can miss the last negotiation.
3. `stop` is `identity`: the contract condition of item 2 holds, and a Discover Identity
   ACK on SOP from the partner has arrived after P, or 8 steps (about 16 s) have passed
   since the first summary that showed the current P. Check for the ACK in each step
   after the contract condition holds with `km003c` `pd_capture_read`, `mode` `messages`,
   `sop` `SOP`, `types` `["Discover_Identity"]`, and `since` P. The partner's ACK is a
   line `Discover_Identity ACK ... from source/...` when the board is sink, and
   `... from sink/...` when it is source; a line from the board's own role is the board's
   identity and does not count.
4. 15 steps (about 30 s) have passed without any P. No contract completed.

Then, if `running` is still true, call `km003c` `pd_capture_stop`. Record the final
summary's `reason`, `counts`, `vbus_mv`, `contract`, `warnings`, and `lines`, and whether
the identity wait ended on the partner's ACK or on its bound.

## Step 5: Sanity rules, before any comparison

Read the summary's `warnings` and `lines`:

| What the summary shows | Conclusion | Action |
|---|---|---|
| No Source_Capabilities, no Request, and VBUS flat (max minus min under 500 mV) | No negotiation happened in this window | Report a stimulus or operator problem, not a protocol bug, and stop |
| Only connection records, no PD messages | No PD traffic, for example a non-PD source. The warning text says PD monitoring was not active; on firmware 2.0.0 the meter captures with monitoring disabled, so read it as no traffic. Quote the header's `pd_monitor` | Report and stop |
| A Request with no Source_Capabilities before it, stimulus other than `aneg` | The capture started late | Compare what remains, mark the verdict inconclusive |
| A Request with no Source_Capabilities before it, stimulus `aneg` | Expected: `aneg` sends Requests only. The meter's "capture started late" warning does not apply | None. The requested PDO comes from the snapshot's 0x30 |
| A `PR_Swap` line followed by an `Accept` | The power roles swapped: the board's role before it is the opposite of the one `bench_hello` reports now. The final contract is the one after the swap | None. `bench_crosscheck` compares the final contract with 0x30 or 0x32 as usual and the board's own Source_Capabilities from before the swap with 0x32 in rows marked "Before PR_Swap"; say so in the report. To keep a sourcing board from swapping, clear PORT_CONTROL (0x29) bits 5 and 4 by hand before the run; this skill does not write 0x29 |
| Lines ending in `(retry)` | Retransmissions; each counts once | None. During a cold restart into PTCH the charger repeats Source_Capabilities about 25 times without a GoodCRC for about 1.8 s before the contract: expected |

When a rule fires and the cause is not clear from the summary, read the Large captures
section.

## Step 6: Snapshot

1. After `restart` or `restart_cold`, call `tps25751-bench` `bench_hello` first. If
   `mode` is PTCH and `patch_pending` is true, take no snapshot (Step 1 item 3 says why):
   report the capture with the summary only, offer `restart_cold`, and stop. If `mode` is
   anything else but APP, report it and stop.
2. Call `tps25751-bench` `bench_snapshot` with `path` set to the capture path with
   `.jsonl` replaced by `.registers.json`. The capture has ended, so PS_RDY and the wait
   of Step 4 have passed. Record `written_to`. Take one snapshot only: it clears
   INT_EVENT1.

## Step 7: Compare

1. Call `tps25751-bench` `bench_crosscheck` with `capture_path` set to the capture's
   absolute path, `registers_path` set to `written_to`, and `stimulus` set to the
   stimulus. It reads the capture's wire hex itself and returns `rows`, `sanity`,
   `verdict`, and `table`. For `aneg` the requested PDO rows say `from 0x30`: they compare
   the snapshot's 0x30 with 0x34, register with register, because the wire carried no
   Source_Capabilities. Without that tool, build the same rows by hand from `km003c`
   `pd_capture_read` with `mode` `facts` and the snapshot, following
   [compare-rules.md](compare-rules.md).
2. Call `km003c` `pd_capture_read` with `mode` `facts` for the meter server's decode of
   each object.
3. For each register a row compares, call `tps25751-bench` `bench_read` with
   `decode` true once: 0x30 or 0x32, 0x34, 0x35, and 0x48 when a partner ACK was
   compared.
4. Decode each compared word three ways, field by field, as
   [compare-rules.md](compare-rules.md) lists: the `facts` decode, the library's
   `decode` lines, and the `usb-pd-docs` table for that object. A word whose hex matches
   can still be decoded wrongly by the library; that is what this step finds.

A disagreement names the word, the field, the three values, and the raw hex, for example
`PDO 4 (000641d6), Maximum Current: facts 4.7 A, library 4.70 A, Table 6.10 bits 9:0 =
0x1d6 = 470 x 10 mA = 4.7 A`.

## Step 8: Report

Report in this order:

1. One line each: meter serial and firmware, board role and `lib`, stimulus and `stop`,
   capture path, snapshot path.
2. The sanity notes, if any, and for `aneg` the `stepped_down` and `restored_contract`
   values.
3. One table, one row per compared or reported word:

   | Word | Wire hex | Register hex | Where | Raw | Fields (facts / library / spec) |
   |---|---|---|---|---|---|

   `Raw` is the row's result from step 7.1. `Fields` is `agree` or names each
   disagreeing field with its three values. Reported rows (the board's own ACK, cable
   ACKs, 0x35 bits 127:96 on a sink, 0x34 bits 41:32 on a source, and the board's PDO 1
   flags in bits 29:23 that differ from 0x32) carry `report` and no field check. Give a
   differing PDO 1 flag its likely runtime source as the row states it, and say it is
   inferred unless the row cites the TRM.
4. The verdict: `pass` when every compared word matches and every field agrees, `fail`
   when any word or field disagrees, `inconclusive` when something needed is missing
   (no completed contract, a late start, a register absent from the snapshot), `no
   negotiation` when a sanity rule stopped the comparison. A `fail` caused only by a
   field disagreement is a library decode bug; name the class and accessor from the
   `decode` line.

## Step 9: Fixture bundle (only with `save_fixture`)

The bundle format is in `test/fixtures/pd-captures/README.md` of the TPS25751 library.

1. In the TPS25751 library itself (it has `test/support/pd_capture_fixture.py`), the
   bundle goes in `test/fixtures/pd-captures/<save_fixture>/`. In any other project, ask
   the user where to put it and skip items 4 to 6.
2. Copy the capture file there as `capture.jsonl`, unchanged, and the snapshot file as
   `registers.json`. Do not take a second snapshot.
3. Write `provenance.json` in the format of the existing bundle's file: source, sink,
   cable, stimulus, meter serial and firmware, operator, and how `registers.json` was
   made (`synthesized` false, `read_from_board_at_capture` true). Record the cable by its
   SOP' Discover Identity VDOs from `facts`, since the bench cable has changed before.
4. Add the bundle's row to the index in the README. `--check` fails for a bundle the
   index does not list.
5. Run `python3 test/support/pd_capture_fixture.py` from the library root, then
   `python3 test/support/pd_capture_fixture.py --check`.
6. Tell the user the native tests that assert the bundle's words by hand from the
   `usb-pd-docs` tables are still to write; the generator does not write them.

## Large captures

Look deeper only when the capture has more than 200 non-GoodCRC messages, when a sanity
rule fired and the summary does not show why, or when the user asks for a
message-by-message review.

- If the Agent tool offers the subagent type `tps25751-bench:pd-capture-analyst`
  (Claude Code with this plugin), give it the capture's absolute path, the question, the
  summary's `warnings`, and the meter-time window of interest. It returns a table of at
  most 30 rows with hex. Do not assume it exists; check the offered types first.
- Otherwise page through the file yourself: `km003c` `pd_capture_read` with `mode`
  `messages`, `limit` 40, `sop` and `types` narrowed to the problem, `since` set to the
  start of the window in meter ms, and, from the second page on, `after` set to the
  previous page's `next_after`. A page without `next_after` is the last. Keep only the
  lines that bear on the problem, stop after five pages, and report how many entries
  (`total` minus what you read) you did not read. Use `mode` `full` with `limit` 5 for
  the full decode of a few messages.
