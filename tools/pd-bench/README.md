# tps25751-pd-bench

An MCP server that lets an agent work a TPS25751 on the bench: read registers raw or
decoded by the library, take the register snapshot that a USB PD capture is checked
against, run 4CC tasks, and make the controller start a fresh negotiation.

The server is the host half of a pair. The firmware half is `TPS25751BenchConsole` in
this library, which answers a line protocol on a serial port. Both halves come from the
same library tag, so the protocol version on each end matches.

## Set up the firmware

The console runs in `examples/pd-bench`, or in any firmware that embeds it with three
lines:

```cpp
#include <TPS25751BenchConsole.h>
TPS25751BenchConsole bench(pd, SerialUSB1);   // any Stream
// in loop():
bench.poll();
```

Flash the example from the library root. Use `example-pd-bench-patch` for a board
without a configuration EEPROM; it compiles in the patch bundle of
`examples/load-patch-bundle`. The console loads the bundle at start (`begin()`) and
after a restart, and only when the controller stays in PTCH: it waits up to 3 s for APP,
and PTCH with INT_EVENT1 Ready for Patch ends that wait early. `HELLO` never loads it.

```bash
pio run -e example-pd-bench -t upload
```

The example builds with `-D USB_DUAL_SERIAL`, so the Teensy enumerates as "Dual Serial"
(VID 0x16C0, PID 0x048B) with two ports. The first carries human-readable logs, for
`pio device monitor`. The second carries the console protocol and belongs to this
server.

## Run the server

From a checkout, in `tools/pd-bench`:

```bash
uv run tps25751-pd-bench
```

From a release tag, with no checkout:

```bash
uvx --from "git+https://github.com/forrestrae/TPS25751@<tag>#subdirectory=tools/pd-bench" tps25751-pd-bench
```

The server speaks MCP over stdio. It needs Python 3.11 or later, `mcp` 2.x, and
`pyserial`.

## Environment

| Variable | Effect |
|---|---|
| `TPS25751_BENCH_PORT` | The console's serial port. Skips discovery. |
| `TPS25751_BENCH_SERIAL` | The Teensy's USB serial number. Needed when more than one Teensy is attached. |
| `TPS25751_BENCH_LOG` | A file that receives every line sent and received, with UTC times, for `tail -f`. |

## Tools

| Tool | Changes the board | What it does |
|---|---|---|
| `bench_hello` | No | Port, protocol and library version, build date, uptime, MODE, VERSION (0x0F), power role, plug presence, patch bundle, and configuration source. Read-only: for a controller in PTCH mode it reports `mode: PTCH` and `patch_pending: true` and loads nothing. |
| `bench_snapshot(path?)` | Clears INT_EVENT1 | Reads 0x03, 0x14, 0x1A, 0x26, 0x30 to 0x35, 0x37, 0x3F, 0x40, 0x42, 0x48, 0x69, and 0x6A in the `tps25751-registers` version 1 format, then clears the INT_EVENT1 bits it reported. With `path`, writes that document as `registers.json`. |
| `bench_read(addr, decode=False)` | No | One register: the length byte as read and the data as hex. With `decode=True`, also the lines of the library's `debugPrint()`. |
| `bench_task(code, data_hex?)` | Yes | Runs one 4CC task and returns its return code and the 64 DATA bytes. |
| `bench_write(addr, hex)` | Yes | Writes the whole of 0x27, 0x28, 0x37, or 0x42 and returns the readback. |
| `bench_renegotiate(method, max_voltage_mv?)` | Yes | Starts a negotiation and returns each step with UTC times. |
| `bench_crosscheck(capture_path, registers_path, role?, stimulus?)` | No, and opens no port | Compares a `km003c-capture` file with a `registers.json` snapshot, word for word, and returns the rows, the sanity notes, a verdict, and a Markdown table. See "Compare a capture with a snapshot". |

A result carries `owner_check: unavailable: ...` when `lsof` is missing or did not
finish, which means the server could not see whether another program has the port open.

`bench_task`, `bench_write`, and `bench_renegotiate` can change the PD contract or drop
VBUS. Agent configurations should ask for approval before each call to them.

### Renegotiation methods

| Method | Role | What happens | VBUS |
|---|---|---|---|
| `gsrc` | Sink | `GSrC`: Get_Source_Cap, Source_Capabilities, Request, Accept, PS_RDY. | Stays, if the same PDO is chosen |
| `aneg` | Sink | Sets a lower host-provided maximum voltage in 0x37, runs `ANeg`, restores 0x37, runs `ANeg` again. Two Requests, no Source_Capabilities. | Steps down and back |
| `ssrc` | Source | `SSrC`: the board sends Source_Capabilities, the partner sends a Request. | Per the new contract |
| `port_reset` | Either | Rewrites 0x28 with its current value and watches STATUS for the disconnect. | Drops to vSafe0V and returns |
| `restart` | Either | `RESTART warm`, which runs `Gaid`. | Drops to vSafe0V and returns |
| `restart_cold` | Either | `RESTART cold`, which runs `GAID` and reloads the patch on a board without an EEPROM. | Drops to vSafe0V and returns |

The call sends `HELLO` first and refuses a method that does not fit: `gsrc` and `aneg`
need the sink role and a plug, `ssrc` the source role and a plug, and every method but
the two restarts needs MODE to read APP. `restart_cold` is refused when the controller's
configuration did not come from an EEPROM and the firmware has no patch bundle. For a
controller that `bench_hello` reports in PTCH mode with `patch_pending`, both restarts
load the bundle once the controller is back in PTCH with Ready for Patch. `restart_cold`
is verified on hardware for that. `restart` is not: a warm restart of a controller
without an EEPROM has not run on hardware.

`aneg` picks its temporary maximum from the registers: the highest Fixed PDO in 0x30
below the voltage of the active PDO in 0x34. With the Apple 140 W charger at 20 V that
is 15 V. `max_voltage_mv` overrides the choice. The method clears Auto Compute Sink Max
Voltage (bit 5) and sets Auto Neg Max Voltage (bits 41:32, 50 mV per LSB), and leaves
every other bit of 0x37 as read, with one exception below. Once the temporary write is
sent, the restore runs on every path, including a failed or timed-out `ANeg`, and the
result says whether 0x37 reads back as the original. If the restore fails, the result
carries the original bytes and the `bench_write` call that puts them back.

Before the first write, `aneg` checks from the registers that the change is safe and
can have an effect:

- Sink path. With Auto Disable Sink Upon Capability Mismatch set (bit 6, reset value 1)
  and No Capability Mismatch clear (bit 3), a contract on a PDO that offers less than the
  capability mismatch power (bits 61:52, 250 mW per LSB) makes the controller open the
  sink path. When the temporary PDO is under that power, the temporary value also clears
  bit 6, and the result has `cleared_bit6: true` with the reason. The restore brings the
  bit back.
- Host minimum. When the host provides the minimum voltage (bit 4 is 0) and that minimum
  (bits 51:42) is above the temporary maximum, no PDO fits and `ANeg` would do nothing,
  so the call is refused with that reason.

After each `ANeg` the method waits 1.5 s and reads 0x34 and 0x35. `stepped_down` says
whether the contract moved to a PDO at or under the temporary maximum, and
`restored_contract` whether the original PDO is active again at the end. Neither makes
the call fail: `stepped_down: false` means the method ran and the controller kept its
contract, so the capture holds no Request from it.

## Compare a capture with a snapshot

`bench_crosscheck` and the command `tps25751-pd-crosscheck` run the same comparison,
`crosscheck.py`. They read the capture's wire hex themselves, so a decode error in the
meter server cannot hide a mismatch, and they follow the fixture generator's rules for
retransmissions and the final contract, counting only Source_Capabilities sent with the
Source power role. Each row compares one wire word with the register bits that should
hold it:

- Source_Capabilities against 0x30 (board sinking) or 0x32 (board sourcing).
- The final Request against 0x35 bits 31:0 (sinking) or 127:96 (sourcing).
- The requested PDO against 0x34 bits 31:0, and PDO 1 bits 29:20 against 0x34 bits
  41:32. That mapping is confirmed only with the board sinking, so a sourcing board's
  row is reported, not compared.
- The partner's Discover Identity ACK on SOP against 0x48.
- The meter's last VBUS against 0x6A, unless STATUS reports no plug.

The board's own ACK and cable ACKs are reported, not compared. The board's role comes
from STATUS (0x1A) bit 5; `--role` is needed only for a snapshot without STATUS. `aneg`
sends Requests without Source_Capabilities, so for it the requested PDO comes from the
snapshot's 0x30, and the rows say so. The stimulus is read from the capture header's
`provenance.stimulus`; `--stimulus` sets it for a capture that does not record it.

```bash
uv run tps25751-pd-crosscheck <capture.jsonl> <registers.json>
```

The verdict is `pass`, `fail`, `inconclusive` (no completed contract, a late start, or a
register missing from the snapshot), or `no negotiation`. The command exits 1 on `fail`
and 2 when a file cannot be read, including a data message shorter than its header
declares; the tool returns the same cases as an error with kind `CrosscheckError`. The `tps25751-pd-verify` skill in
`plugins/tps25751-bench` drives the whole check and adds the field-by-field decode.

## Copy the skill into a downstream project

Codex reads skills from `<project>/.agents/skills/`. This library links its own entry to
the plugin's skill directory; a project that consumes the library gets a copy instead,
written from a library checkout at the tag the project pins, so the skill matches the
bench server from that tag. Run it from the library root:

```bash
python3 tools/pd-bench/sync_skill.py --dest <project>
```

`<project>` is the downstream project's root. The script replaces
`<project>/.agents/skills/tps25751-pd-verify` as a whole and records the library version
and commit in `.synced-from.json` inside it. It refuses a directory it did not write, and
a symlink, unless `--force` is given, and refuses a `.agents` or `.agents/skills` that
links into the library even then. The old copy is removed only after the new one is in
place. `--dry-run` lists the files without writing.
Claude Code projects get the skill from the plugin instead (Phase 8 of the toolset plan).

## How the server finds and shares the port

Discovery lists the serial ports with the Teensy VID 0x16C0. `TPS25751_BENCH_SERIAL`
narrows them to one board; two boards without it is an error. A dual-serial Teensy has
two ports, and `pyserial` reports no interface number on macOS, so the server takes the
console to be the port on the higher USB interface: the device name that sorts last on
macOS (`/dev/cu.usbmodem<serial>3` against `...1`), or the higher interface number in
the Linux location string. It confirms the choice with `HELLO`, and tries the other port
only if the first stays silent. `TPS25751_BENCH_PORT` replaces all of this.

The probe writes one `@<id> HELLO` line to a Teensy port that no other process holds.
Firmware without the console sees that line on its serial input. Set
`TPS25751_BENCH_PORT` if a Teensy on the machine must not receive it.

Each tool call owns the port for its own length:

1. It runs `lsof` on the port. If another process has it open, the call fails and names
   the process, for example `pio device monitor`. The server does not open or probe a
   port another process holds.
2. It opens the port with `exclusive=True`. That is an advisory lock a serial monitor
   does not take, which is why the `lsof` check comes first.
3. It sends `HELLO` on the first call, and again after a timeout, a port error, or a
   protocol error, and refuses a console that reports another protocol version.
   `bench_hello` and `bench_renegotiate` send it on every call. An `ERR` reply to a
   command, or a busy port, does not cause a new `HELLO`.
4. It runs the command and closes the port.

Nothing holds the port between calls, so `pio run -t upload` works at any time.

When `lsof` is missing or does not finish, step 1 cannot run. The call then proceeds
under the exclusive lock alone and says so in its result as `owner_check`.

The first `HELLO` waits up to 10 s for an answer. Right after a reset the console does
not read the port while `begin()` runs, and on a board without an EEPROM that includes
the patch load, a few seconds when it succeeds. A load that fails can run to the caps of
the loader's waits, about 30 s in all. A console that stays silent longer than 10 s is
reported as busy or still starting; call again.

## Protocol version 1

```
request  @<id> <COMMAND> [arguments]
reply    @<id> <text>                  zero or more lines
final    @<id> OK
         @<id> ERR <CODE> <text>
event    ! <text>
```

A line that starts with neither `@` nor `!` is log output from the firmware; the server
returns such lines under `log` and events under `events`. Reply text is space-separated
`key=value` tokens, or `| ` followed by one line of decode text. Hex is lowercase.

| Request | Reply lines before `OK` |
|---|---|
| `@1 HELLO` | `proto=1 lib=3.1.0 build=Nov_23_2026_10:11:12 uptime_ms=5000`, `mode=APP version=f8091166 role=sink plug=1`, `patch_bundle=0 patch_pending=0 patch_source=eeprom swaps=0` |
| `@2 RD 0x34` | `reg=0x34 len=6 data=d64106009800` |
| `@3 DEC 0x1A` | The `RD` line, then `\| === Status Register (0x1A) ===` and the rest of the decode |
| `@4 SNAP` | `uptime_ms=5000`, one `RD` line per register, `int_clear=ok` |
| `@5 WR 0x42 1a000800` | `reg=0x42 len=4 data=1a000800 match=1` |
| `@6 CMD GSrC` | `task=GSrC status=success rc=0x00`, `data=` and 64 bytes of hex |
| `@7 RESTART warm` | `step=issue task=Gaid status=timeout t_ms=5105`, `step=nak t_ms=6005`, `step=back evidence=data_cleared t_ms=6305`, `step=mode t_ms=8005 mode=APP waited_ms=1700`, `step=done mode=APP t_ms=8005` |

After a restart the controller answers on I2C before it is ready: on the bench board
COMMAND and DATA read within 30 ms of the warm restart, MODE gave nothing for several
seconds, and the port detached and attached a second time. `RESTART` therefore polls
MODE every 50 ms after `step=back`, for up to 10 s, treating read errors, an empty MODE,
and BOOT as "not yet". `step=mode` reports the mode and how long it took; if neither APP
nor PTCH appears, the reply is `ERR TIMEOUT` naming the last read. `HELLO` reads MODE up
to three times, 100 ms apart, before it reports `mode=?`.

A patch load does not start because MODE read PTCH once: a controller with an EEPROM reads
PTCH on its way to APP. The console gives the controller 3 s to reach APP, and PTCH with
INT_EVENT1 Ready for Patch ends that wait early. `TPS25751PatchLoader` then polls Ready
for Patch and MODE itself and sends nothing without them, so a PTCH without Ready for
Patch ends as `CommandTimeout`, not as a load. The server waits 60 s for a `RESTART`,
against about 50 s inside the console when every wait runs to its cap.

`RESTART` reports the loader's outcome in a `step=patch` line and `begin()` in a
`! PATCH` event:

```
@7 step=patch result=<status> t_ms=<ms> sent=<0|1>
! PATCH result=<status> mode=<MODE> sent=<0|1>
```

`sent=0` says the loader ended in its Ready for Patch or MODE check, before it wrote
anything to the controller. If MODE then reads APP, the controller loaded its own
configuration while the loader waited, and the restart succeeds: the line
`step=patch result=CommandTimeout t_ms=14305 sent=0` is followed by `step=done mode=APP`
and `OK`. A load with `sent=1` that does not end as `Success` fails with `ERR PATCH`,
even when MODE reads APP. The status alone does not separate the two cases:
`CommandTimeout` is also a `PBMs` or `PBMc` that did not complete.

Ready for Patch is INT_EVENT1 bit 81. Anything that clears it while the controller sits
in PTCH leaves the next load without it, so that load ends with `sent=0` and the
controller stays in PTCH. `SNAP` does this: it writes INT_CLEAR1 with the bits it read.
`RESTART cold` is the recovery verified on hardware.

Error codes: `SYNTAX`, `TOOLONG`, `UNKNOWN`, `BADARG`, `REFUSED`, `I2C`, `TIMEOUT`,
`REJECTED`, `NOBUNDLE`, `PATCH`, and `NODECODER`.

`CMD` allows `GSrC`, `GSkC`, `ANeg`, `SSrC`, `GPPI`, `MBRd`, `SRDY`, `SRYR`, `DBfg`, and
`I2Cr`. `Gaid` and `GAID` run the restart sequence of `RESTART`. The swaps `SWSk`,
`SWSr`, `SWDF`, and `SWUF` need firmware built with `-D TPS25751_BENCH_ALLOW_SWAPS`. The
console refuses the patch tasks, `GO2P`, the EEPROM tasks, `I2Cw`, and every other code.
The console does not pace `I2Cr`; the TRM asks for 5 s between two of them.

## Tests

The tests use a simulated console behind a fake serial port and never open a real one.
The comparison and `sync_skill.py` tests use files in a temporary directory.
The verify gate of the library runs them with the locked dependencies:

```bash
uv run --locked pytest
```
