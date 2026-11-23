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
`examples/load-patch-bundle`, which the console reloads whenever MODE reads PTCH.

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
| `bench_hello` | No | Port, protocol and library version, build date, uptime, MODE, VERSION (0x0F), power role, plug presence, patch bundle, and configuration source. |
| `bench_snapshot(path?)` | Clears INT_EVENT1 | Reads 0x03, 0x14, 0x1A, 0x26, 0x30 to 0x35, 0x37, 0x3F, 0x40, 0x42, 0x48, 0x69, and 0x6A in the `tps25751-registers` version 1 format, then clears the INT_EVENT1 bits it reported. With `path`, writes that document as `registers.json`. |
| `bench_read(addr, decode=False)` | No | One register: the length byte as read and the data as hex. With `decode=True`, also the lines of the library's `debugPrint()`. |
| `bench_task(code, data_hex?)` | Yes | Runs one 4CC task and returns its return code and the 64 DATA bytes. |
| `bench_write(addr, hex)` | Yes | Writes the whole of 0x27, 0x28, 0x37, or 0x42 and returns the readback. |
| `bench_renegotiate(method, max_voltage_mv?)` | Yes | Starts a negotiation and returns each step with UTC times. |

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
configuration did not come from an EEPROM and the firmware has no patch bundle.

`aneg` picks its temporary maximum from the registers: the highest Fixed PDO in 0x30
below the voltage of the active PDO in 0x34. With the Apple 140 W charger at 20 V that
is 15 V. `max_voltage_mv` overrides the choice. The method clears Auto Compute Sink Max
Voltage (bit 5) and sets Auto Neg Max Voltage (bits 41:32, 50 mV per LSB), and leaves
every other bit of 0x37 as read. Once the temporary write is sent, the restore runs on
every path, including a failed or timed-out `ANeg`, and the result says whether 0x37
reads back as the original. If the restore fails, the result carries the original bytes
and the `bench_write` call that puts them back.

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
3. It sends `HELLO` on the first call, and again after any failure, and refuses a
   console that reports another protocol version.
4. It runs the command and closes the port.

Nothing holds the port between calls, so `pio run -t upload` works at any time.

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
| `@1 HELLO` | `proto=1 lib=3.1.0 build=Nov_23_2026_10:11:12 uptime_ms=5000`, `mode=APP version=f8091166 role=sink plug=1`, `patch_bundle=0 patch_source=eeprom swaps=0` |
| `@2 RD 0x34` | `reg=0x34 len=6 data=d64106009800` |
| `@3 DEC 0x1A` | The `RD` line, then `\| === Status Register (0x1A) ===` and the rest of the decode |
| `@4 SNAP` | `uptime_ms=5000`, one `RD` line per register, `int_clear=ok` |
| `@5 WR 0x42 1a000800` | `reg=0x42 len=4 data=1a000800 match=1` |
| `@6 CMD GSrC` | `task=GSrC status=success rc=0x00`, `data=` and 64 bytes of hex |
| `@7 RESTART warm` | `step=issue task=Gaid status=timeout t_ms=5105`, `step=nak t_ms=6005`, `step=back evidence=data_cleared t_ms=6305`, `step=done mode=APP t_ms=6305` |

Error codes: `SYNTAX`, `TOOLONG`, `UNKNOWN`, `BADARG`, `REFUSED`, `I2C`, `TIMEOUT`,
`REJECTED`, `NOBUNDLE`, `PATCH`, and `NODECODER`.

`CMD` allows `GSrC`, `GSkC`, `ANeg`, `SSrC`, `GPPI`, `MBRd`, `SRDY`, `SRYR`, `DBfg`, and
`I2Cr`. `Gaid` and `GAID` run the restart sequence of `RESTART`. The swaps `SWSk`,
`SWSr`, `SWDF`, and `SWUF` need firmware built with `-D TPS25751_BENCH_ALLOW_SWAPS`. The
console refuses the patch tasks, `GO2P`, the EEPROM tasks, `I2Cw`, and every other code.
The console does not pace `I2Cr`; the TRM asks for 5 s between two of them.

## Tests

The tests use a simulated console behind a fake serial port and never open a real one.
The verify gate of the library runs them with the locked dependencies:

```bash
uv run --locked pytest
```
