"""MCP server for the TPS25751 bench console.

The console is ``TPS25751BenchConsole`` in the library, running in
``examples/pd-bench`` or in any firmware that embeds it. Each tool call finds the
console's serial port, checks that no other process has it open, opens it with an
exclusive lock, sends ``HELLO`` on first use, runs its command, and closes the
port again.

Environment:

- ``TPS25751_BENCH_PORT``: the console's serial port; skips discovery.
- ``TPS25751_BENCH_SERIAL``: the Teensy's USB serial number, when several are attached.
- ``TPS25751_BENCH_LOG``: a file that receives all serial traffic, for ``tail -f``.

``HELLO`` is sent again after a timeout, a port error, or a protocol error, and
by ``bench_hello`` and ``bench_renegotiate`` on every call.
"""

from __future__ import annotations

import contextlib
import functools
import os
import subprocess
import threading
import time
from dataclasses import dataclass
from types import SimpleNamespace
from typing import Callable, Iterable, Iterator

import serial
from mcp.server.mcpserver import MCPServer
from mcp.types import ToolAnnotations
from serial.tools import list_ports

from . import PROTOCOL_VERSION, registers
from .link import FIRST_LINE_TIMEOUT_S, BenchLink, TrafficLog, utc_now
from .ports import PortCandidate, find_candidates, port_holders
from .protocol import (
    BenchBusyError,
    BenchCommandError,
    BenchError,
    BenchPortError,
    BenchProtocolError,
    BenchRefusedError,
    BenchTimeoutError,
    Reply,
    check_version,
    clean_hex,
    parse_address,
    parse_tokens,
)

# Reply deadlines in seconds. Each is the console's own worst case plus margin.
HELLO_TIMEOUT_S = 20.0     # the whole reply; the console only reads four registers for it
READ_TIMEOUT_S = 3.0
SNAPSHOT_TIMEOUT_S = 6.0
WRITE_TIMEOUT_S = 4.0
TASK_TIMEOUT_S = 4.0       # the console waits up to 2 s for a PD message task
GPPI_TIMEOUT_S = 8.0       # and up to 5 s for GPPI

# The longest a RESTART can take inside the console before it answers: 0.1 s to
# issue the task, 10 s for the controller to restart, 10 s for MODE to read APP
# or PTCH afterwards, 0.4 s of MODE retries, 3 s for APP or PTCH readiness, and
# the patch load. The loader caps each of its waits (TPS25751PatchLoader.h):
# Ready for Patch 5 s, PTCH 5 s, PBMs 0.5 s, the burst about 1 s, PBMc 5 s, the
# DATA read 5 s, and APP 5 s. The host waits longer.
PATCH_LOAD_WORST_CASE_S = 5.0 + 5.0 + 0.5 + 1.0 + 5.0 + 5.0 + 5.0
CONSOLE_RESTART_WORST_CASE_S = 0.1 + 10.0 + 10.0 + 0.4 + 3.0 + PATCH_LOAD_WORST_CASE_S
RESTART_TIMEOUT_S = 60.0

ANEG_SETTLE_S = 1.5        # lets a contract reach PS_RDY before it is read back or changed again
PORT_RESET_WATCH_S = 3.0   # how long port_reset watches STATUS for the disconnect
PORT_RESET_POLL_S = 0.1

RENEGOTIATE_METHODS = {
    "gsrc": "Sink only. Runs GSrC: Get_Source_Cap, Source_Capabilities, Request, Accept, PS_RDY. "
            "VBUS stays at the contract voltage if the same PDO is chosen. Updates 0x30.",
    "aneg": "Sink only. Lowers the maximum voltage in 0x37 to the source's next lower Fixed PDO, "
            "runs ANeg, restores 0x37, and runs ANeg again: two Request, Accept, PS_RDY sequences "
            "and two VBUS steps, with no Source_Capabilities. stepped_down and restored_contract "
            "say whether 0x34 followed.",
    "ssrc": "Source only. Runs SSrC: the board sends Source_Capabilities and the partner sends a "
            "Request.",
    "port_reset": "Either role. Rewrites 0x28 with its current value, which the TRM says "
                  "disconnects and reconnects the port: VBUS drops to vSafe0V and a full attach "
                  "sequence follows.",
    "restart": "Either role. Runs Gaid: Type-C Error Recovery for about 1 s, a warm restart, and a "
               "fresh attach. VBUS drops to vSafe0V. Host-written registers revert.",
    "restart_cold": "Either role. Runs GAID: like restart, through the OTP bootloader. A board "
                    "without a configuration EEPROM comes back in PTCH and the console reloads "
                    "its patch bundle.",
}
SINK_METHODS = {"gsrc", "aneg"}
SOURCE_METHODS = {"ssrc"}


@dataclass
class Session:
    """An open link and the console's HELLO reply, for the length of one tool call."""

    link: BenchLink
    hello: dict


def parse_hello(reply: Reply, port: str) -> dict:
    """The HELLO reply as a dict, after checking the protocol version."""
    tokens = reply.tokens()
    check_version(tokens)

    def flag(name: str) -> bool | None:
        return {"1": True, "0": False}.get(tokens.get(name, ""))

    hello = {
        "port": port,
        "proto": PROTOCOL_VERSION,
        "lib": tokens.get("lib", "unknown"),
        "build": " ".join(tokens.get("build", "").split("_")) or None,
        "uptime_ms": int(tokens["uptime_ms"]) if tokens.get("uptime_ms", "").isdigit() else None,
        "mode": tokens.get("mode", "?"),
        "version": tokens.get("version", "?"),
        "role": tokens.get("role", "?"),
        "plug": flag("plug"),
        "patch_bundle": flag("patch_bundle"),
        "patch_pending": flag("patch_pending"),
        "patch_source": tokens.get("patch_source", "?"),
        "swaps": flag("swaps"),
    }
    if hello["mode"] == "?":
        hello["warning"] = "the console answers but the TPS25751 does not answer on I2C"
    elif hello["patch_pending"]:
        hello["warning"] = ("MODE is PTCH and the firmware holds a patch bundle. HELLO does not load "
                            "it; bench_renegotiate('restart_cold') does (verified on hardware), and so does a reset of "
                            "the Teensy")
    return hello


class BenchService:
    """Port discovery, the single-owner rule, and the HELLO handshake."""

    def __init__(self, *, env: dict | None = None,
                 serial_factory: Callable[..., object] = serial.Serial,
                 comports: Callable[[], Iterable] = list_ports.comports,
                 run: Callable[..., object] = subprocess.run,
                 clock: Callable[[], float] = time.monotonic,
                 sleep: Callable[[float], None] = time.sleep,
                 now_utc: Callable[[], str] = utc_now) -> None:
        env = os.environ if env is None else env
        self.port_override = env.get("TPS25751_BENCH_PORT") or None
        self.serial_filter = env.get("TPS25751_BENCH_SERIAL") or None
        self.traffic = TrafficLog(env.get("TPS25751_BENCH_LOG") or None)
        self._serial_factory = serial_factory
        self._comports = comports
        self._run = run
        self.clock = clock
        self.sleep = sleep
        self.now_utc = now_utc
        self._port: str | None = None   # the port a HELLO confirmed as the console
        self._hello: dict | None = None
        self.owner_check: str | None = None  # set when lsof could not check the port's owner
        self._lock = threading.Lock()   # one tool call owns the port at a time

    def forget(self) -> None:
        """Drop the confirmed port, so the next call discovers and greets again."""
        self._port = None
        self._hello = None

    @contextlib.contextmanager
    def session(self, fresh_hello: bool = False) -> Iterator[Session]:
        """Open the console for one tool call. The port is closed on exit, whatever happens."""
        with self._lock:
            link, hello = self._open(fresh_hello)
            try:
                yield Session(link, hello)
            except (BenchTimeoutError, BenchPortError, BenchProtocolError):
                self.forget()  # the next call starts from discovery and HELLO
                raise
            finally:
                link.close()

    def _hello_on(self, link: BenchLink) -> dict:
        reply = link.command("HELLO", HELLO_TIMEOUT_S, first_line_timeout_s=FIRST_LINE_TIMEOUT_S)
        return parse_hello(reply, link.port)

    def _open(self, fresh_hello: bool, rediscovered: bool = False) -> tuple[BenchLink, dict]:
        confirmed = self._port
        if confirmed:
            candidates = [PortCandidate(device=confirmed, designated=True)]
        else:
            candidates = find_candidates(port=self.port_override, serial_number=self.serial_filter,
                                         comports=self._comports)
        silent: list[str] = []
        busy: list[str] = []
        self.owner_check = None
        for candidate in candidates:
            # Another reader would take part of the console's replies, and a
            # probe would put a request into its session, so a held port is
            # never opened.
            check = port_holders(candidate.device, run=self._run)
            if check.unavailable:
                # Not the same as "no holder": say so with the result, and rely
                # on the exclusive lock alone.
                self.owner_check = (f"unavailable: {check.unavailable}; only the exclusive lock "
                                    "guards the port, which a serial monitor does not take")
            holders = check.holders
            if holders:
                message = f"{candidate.device} is open in {', '.join(holders)}"
                if candidate.designated:
                    raise BenchBusyError(message + ". Close that program, or point it at the "
                                                   "log port, and call again")
                busy.append(message)
                continue

            link = BenchLink(candidate.device, serial_factory=self._serial_factory,
                             traffic=self.traffic, clock=self.clock, now_utc=self.now_utc)
            try:
                link.open()
            except BenchBusyError as exc:
                if candidate.designated:
                    raise
                busy.append(str(exc))
                continue
            except BenchPortError as exc:
                if confirmed and not rediscovered:
                    # The port a previous call used is gone, for example after a
                    # firmware upload under another name. Look again once.
                    self.forget()
                    return self._open(fresh_hello, rediscovered=True)
                silent.append(str(exc))
                continue

            if confirmed and self._hello is not None and not fresh_hello:
                return link, self._hello
            try:
                hello = self._hello_on(link)
            except BenchTimeoutError as exc:
                link.close()
                if exc.answered:
                    raise
                silent.append(f"{candidate.device} did not answer HELLO within "
                              f"{FIRST_LINE_TIMEOUT_S:g} s")
                continue
            except BaseException:
                link.close()
                raise
            self._port, self._hello = candidate.device, hello
            return link, hello

        self.forget()
        if busy and not silent:
            raise BenchBusyError("; ".join(busy) + ". Close that program and call again")
        raise BenchPortError(
            "no bench console answered: " + "; ".join(silent + busy)
            + ". A console that stays silent this long is busy or still starting (after a reset its "
              "begin() can load the patch for several seconds before it reads the port), or the "
              "firmware has no console: is examples/pd-bench flashed, and does it call poll()?")


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

def _register_from(reply: Reply) -> dict:
    for line in reply.data_lines():
        tokens = parse_tokens(line)
        if "reg" in tokens and "len" in tokens:
            address, length, data = registers.parse_register_tokens(tokens)
            entry = registers.register_entry(address, length, data)
            if "match" in tokens:
                entry["match"] = tokens["match"] == "1"
            return entry
    raise BenchProtocolError(f"the reply to '{reply.request}' has no register line")


def _read(session: Session, address: int) -> dict:
    return _register_from(session.link.command(f"RD 0x{address:02x}", READ_TIMEOUT_S))


def _write(session: Session, address: int, data_hex: str) -> dict:
    return _register_from(session.link.command(f"WR 0x{address:02x} {data_hex}", WRITE_TIMEOUT_S))


def _task_timeout(code: str) -> float:
    if code in ("Gaid", "GAID"):
        return RESTART_TIMEOUT_S
    return GPPI_TIMEOUT_S if code == "GPPI" else TASK_TIMEOUT_S


def _task_result(reply: Reply, code: str) -> dict:
    tokens = reply.tokens()
    out: dict = {"task": code}
    if "rc" in tokens:
        return_code = int(tokens["rc"], 16)
        out["return_code"] = return_code
        meaning = registers.task_result(code, return_code)
        if meaning is not None:
            out["task_result"] = meaning
            out["task_ok"] = (return_code & 0x0F) == 0
    if "data" in tokens:
        out["data"] = tokens["data"]
    steps = _restart_steps(reply)
    if steps:
        out["restart_steps"] = steps
    return out


def _restart_steps(reply: Reply) -> list[dict]:
    """The ``step=`` lines of a restart reply.

    ``t_ms`` is the console's uptime clock. ``step=mode`` carries ``waited_ms``:
    how long after the controller answered again MODE took to read APP or PTCH.
    ``step=patch`` carries the patch loader's ``result`` and ``sent``: false when
    the loader ended before it wrote anything to the controller. With ``sent``
    false and a ``step=done`` after it, the controller reached APP by itself and
    ``result`` is for information only.
    """
    steps = []
    for line in reply.data_lines():
        tokens: dict = parse_tokens(line)
        if "step" in tokens:
            for name in ("t_ms", "waited_ms"):
                if tokens.get(name, "").isdigit():
                    tokens[name] = int(tokens[name])
            if tokens.get("sent") in ("0", "1"):
                tokens["sent"] = tokens["sent"] == "1"
            steps.append(tokens)
    return steps


def build_tools(service: BenchService) -> SimpleNamespace:
    """The tool functions, bound to ``service``."""

    def call(fn, args, kwargs) -> dict:
        try:
            return fn(*args, **kwargs)
        except BenchCommandError as exc:
            out = {"error": str(exc), "kind": type(exc).__name__, "code": exc.code}
            if exc.reply.lines:
                out["reply_lines"] = exc.reply.lines[-40:]
            out.update(exc.reply.extras())
            return out
        except (BenchError, serial.SerialException, ValueError, OSError) as exc:
            return {"error": str(exc), "kind": type(exc).__name__}

    def safe(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            service.owner_check = None
            result = call(fn, args, kwargs)
            if service.owner_check:
                # lsof could not say who has the port open; a silent "no
                # holder" would overstate what was checked.
                result["owner_check"] = service.owner_check
            return result
        return wrapper

    @safe
    def bench_hello() -> dict:
        """Identify the bench console and the TPS25751 behind it.

        Read-only: the console reads MODE, VERSION, STATUS, and BOOT_FLAGS and
        changes nothing. Returns the serial port, protocol version, library
        version, firmware build date, uptime in ms, MODE (APP, PTCH, BOOT, or ? when
        the controller does not answer on I2C), VERSION (0x0F) as hex, the power
        role (sink or source) and whether a plug is attached, whether a patch
        bundle is compiled in, where the running configuration came from (eeprom,
        i2c, none), and whether the firmware allows role swaps. patch_pending is
        true when MODE reads PTCH and a bundle is compiled in: the controller
        waits for the patch, which bench_renegotiate("restart_cold") loads.
        """
        with service.session(fresh_hello=True) as session:
            return dict(session.hello)

    @safe
    def bench_snapshot(path: str | None = None) -> dict:
        """Read the cross-check registers in one pass and clear INT_EVENT1.

        Returns 0x03, 0x14, 0x1A, 0x26, 0x30 to 0x35, 0x37, 0x3F, 0x40, 0x42, 0x48,
        0x69, and 0x6A as raw bytes in the tps25751-registers version 1 format: each
        register's address, name, the I2C length byte as the controller sent it,
        and lowercase hex data. The console then clears exactly the INT_EVENT1 bits
        it reported, so the next snapshot shows only what happened since.

        With path, also writes the registers.json document there (a directory gets
        registers.json inside it); an existing file is replaced. Take the snapshot
        after PS_RDY when comparing with a capture.
        """
        with service.session() as session:
            reply = session.link.command("SNAP", SNAPSHOT_TIMEOUT_S)
        tokens = reply.tokens()
        entries = []
        for line in reply.data_lines():
            fields = parse_tokens(line)
            if "reg" in fields and "len" in fields:
                entries.append(registers.register_entry(*registers.parse_register_tokens(fields)))
        document = registers.snapshot_document(reply.finished_utc, entries)
        result = dict(document)
        result["int_event1_cleared"] = tokens.get("int_clear") == "ok"
        if tokens.get("uptime_ms", "").isdigit():
            result["uptime_ms"] = int(tokens["uptime_ms"])
        if path:
            registers.check_snapshot_registers(entries)
            written, replaced = registers.write_snapshot(path, document)
            result["written_to"] = written
            result["replaced_existing_file"] = replaced
        result.update(reply.extras())
        return result

    @safe
    def bench_read(addr: int | str, decode: bool = False) -> dict:
        """Read one TPS25751 register as raw bytes, optionally with the library's decode.

        addr is the register address, as a number or a string such as "0x34".
        Returns the address, name, the I2C length byte as read, and lowercase hex
        data. With decode=True, also returns decode: the lines of the library's
        debugPrint() for that register, which is the library's own reading of the
        bytes. A register the library has no class for returns the raw bytes and
        decode_error.
        """
        address = parse_address(addr)
        with service.session() as session:
            if not decode:
                return _read(session, address)
            reply = session.link.command(f"DEC 0x{address:02x}", READ_TIMEOUT_S, check=False)
        if not reply.ok and reply.error_code != "NODECODER":
            raise BenchCommandError(reply)
        result = _register_from(reply)
        if reply.ok:
            result["decode"] = reply.decode_text()
        else:
            result["decode_error"] = reply.error_text
        result.update(reply.extras())
        return result

    @safe
    def bench_task(code: str, data_hex: str | None = None) -> dict:
        """Run one 4CC task on the TPS25751. Can change the contract or drop VBUS.

        code is the four-character task code, case-sensitive. data_hex is the
        task's input for the DATA register, up to 64 bytes. The console allows
        GSrC, GSkC, ANeg, SSrC, GPPI, MBRd, SRDY, SRYR, DBfg, and I2Cr; Gaid and
        GAID restart the controller (and reload the patch when needed); the role
        swaps need firmware built with -D TPS25751_BENCH_ALLOW_SWAPS. It refuses
        the patch tasks, GO2P, the EEPROM tasks, I2Cw, and anything else.

        Returns the task's return code with its meaning, and the 64 DATA bytes as
        hex. Prefer bench_renegotiate for starting a negotiation: it checks the
        power role first and reports the steps with times.
        """
        if len(code) != 4 or not all(" " < c <= "~" for c in code):
            raise ValueError("a task code is four printable ASCII characters, such as GSrC")
        command = f"CMD {code}"
        if data_hex:
            payload = clean_hex(data_hex, "data_hex")
            if len(payload) > 128:
                raise ValueError("data_hex holds more than the 64 bytes of the DATA register")
            command += f" {payload}"
        with service.session() as session:
            reply = session.link.command(command, _task_timeout(code))
        result = _task_result(reply, code)
        result["sent_utc"] = reply.sent_utc
        result["finished_utc"] = reply.finished_utc
        result.update(reply.extras())
        return result

    @safe
    def bench_write(addr: int | str, hex: str) -> dict:  # noqa: A002 (the tool's argument name)
        """Write a whole TPS25751 register and return the readback. Can drop VBUS.

        The console allows 0x27, 0x28, 0x37, and 0x42 only. hex must be the whole
        register, as many bytes as its length byte. A write to 0x27 or 0x28
        disconnects and reconnects the port. Start a 0x27 write from a fresh
        bench_read: the firmware keeps values in bits the TRM calls reserved.
        Host-written values last until the next restart.

        Returns the register as read back and match, which is false when the
        controller kept other bytes than the ones written.
        """
        address = parse_address(addr)
        payload = clean_hex(hex, "hex")
        if not payload:
            raise ValueError("hex is empty; give the whole register")
        with service.session() as session:
            reply = session.link.command(f"WR 0x{address:02x} {payload}", WRITE_TIMEOUT_S)
        result = _register_from(reply)
        result["written"] = payload
        result["sent_utc"] = reply.sent_utc
        result["finished_utc"] = reply.finished_utc
        result.update(reply.extras())
        return result

    @safe
    def bench_renegotiate(method: str, max_voltage_mv: int | None = None) -> dict:
        """Make the TPS25751 start a USB PD negotiation. Changes the contract; some methods drop VBUS.

        method is one of:
        - gsrc (board is sink): GSrC. Get_Source_Cap, Source_Capabilities, Request,
          Accept, PS_RDY. The least disruptive.
        - aneg (board is sink): lowers the maximum voltage in 0x37, runs ANeg,
          restores 0x37, runs ANeg again. Two Requests, two VBUS steps, no
          Source_Capabilities. max_voltage_mv sets the temporary maximum; by
          default it is the source's next Fixed PDO below the contract voltage.
          The result reports stepped_down and restored_contract from 0x34, and
          cleared_bit6 when the temporary value also had to keep the sink path
          closed against a capability mismatch.
        - ssrc (board is source): SSrC. The board sends Source_Capabilities.
        - port_reset: rewrites 0x28 with its current value. Disconnect and
          reconnect; VBUS drops.
        - restart: Gaid, a warm restart. VBUS drops for about 1 s. The port can
          detach and attach twice, and the console waits up to 10 s for MODE to
          read APP before it answers.
        - restart_cold: GAID, a cold restart with a patch reload on a board
          without a configuration EEPROM. VBUS drops.

        The call reads the power role first and refuses a method that does not fit
        it. Start the capture and place its mark before calling. Returns the role,
        each step with UTC send and finish times, and what to expect on the wire.
        """
        method = method.strip().lower()
        if method not in RENEGOTIATE_METHODS:
            raise ValueError(f"unknown method {method!r}; use one of " + ", ".join(RENEGOTIATE_METHODS))

        result: dict = {"method": method, "expect": RENEGOTIATE_METHODS[method], "steps": []}
        steps: list[dict] = result["steps"]

        def step(name: str, reply: Reply | None, **details) -> dict:
            entry = {"step": name}
            if reply is not None:
                entry["sent_utc"] = reply.sent_utc
                entry["finished_utc"] = reply.finished_utc
            entry.update(details)
            steps.append(entry)
            return entry

        def run_task(session: Session, code: str) -> dict:
            reply = session.link.command(f"CMD {code}", _task_timeout(code))
            details = _task_result(reply, code)
            details.pop("data", None)
            details.pop("task", None)
            return step(f"CMD {code}", reply, **details)

        try:
            with service.session(fresh_hello=True) as session:
                hello = session.hello
                result.update(role=hello["role"], plug=hello["plug"], mode=hello["mode"],
                              started_utc=service.now_utc())
                _check_method_fits(method, hello)

                if method == "gsrc":
                    run_task(session, "GSrC")
                elif method == "ssrc":
                    run_task(session, "SSrC")
                elif method == "aneg":
                    _renegotiate_aneg(service, session, step, run_task, max_voltage_mv, result)
                elif method == "port_reset":
                    _renegotiate_port_reset(service, session, step, result)
                else:
                    cold = method == "restart_cold"
                    reply = session.link.command("RESTART cold" if cold else "RESTART warm",
                                                 RESTART_TIMEOUT_S)
                    step(reply.request, reply, restart_steps=_restart_steps(reply))
        except BenchCommandError as exc:
            result.update(error=str(exc), kind=type(exc).__name__, code=exc.code)
            if exc.reply.lines:
                result["reply_lines"] = exc.reply.lines[-40:]
        except (BenchError, serial.SerialException, OSError) as exc:
            result.update(error=str(exc), kind=type(exc).__name__)
        result["finished_utc"] = service.now_utc()
        return result

    return SimpleNamespace(bench_hello=bench_hello, bench_snapshot=bench_snapshot,
                           bench_read=bench_read, bench_task=bench_task, bench_write=bench_write,
                           bench_renegotiate=bench_renegotiate)


def _check_method_fits(method: str, hello: dict) -> None:
    """Refuse a method the board's present state cannot carry out."""
    restart = method in ("restart", "restart_cold")
    if hello["mode"] != "APP" and not restart:
        raise BenchRefusedError(f"MODE is {hello['mode']}, not APP; only restart and restart_cold "
                                "make sense now")
    if method in SINK_METHODS or method in SOURCE_METHODS:
        needed = "sink" if method in SINK_METHODS else "source"
        if hello["plug"] is not True:
            raise BenchRefusedError(f"{method} needs a port partner and STATUS reports no plug")
        if hello["role"] != needed:
            other = "ssrc" if needed == "sink" else "gsrc or aneg"
            raise BenchRefusedError(f"{method} needs the board to be {needed} and it is "
                                    f"{hello['role']}; use {other}")
    if method == "restart_cold" and hello["patch_bundle"] is False and hello["patch_source"] != "eeprom":
        raise BenchRefusedError("restart_cold would leave the controller in PTCH: its configuration "
                                "did not come from an EEPROM and the firmware has no patch bundle "
                                "(build example-pd-bench-patch)")


def _contract(session: Session) -> dict:
    """The active PDO (0x34) and the RDO this device sent (0x35), for a before and after comparison."""
    pdo = registers.active_pdo(bytes.fromhex(_read(session, 0x34)["data"]))
    rdo = registers.own_rdo(bytes.fromhex(_read(session, 0x35)["data"]))
    return {"pdo": f"0x{pdo:08X}", "mv": registers.fixed_pdo_voltage_mv(pdo), "rdo": f"0x{rdo:08X}"}


def _contract_or_error(session: Session) -> dict:
    """Like ``_contract``, but a failed read becomes an entry in the result instead of an exception."""
    try:
        return _contract(session)
    except (BenchError, serial.SerialException, OSError, ValueError) as exc:
        return {"error": str(exc)}


def _renegotiate_aneg(service: BenchService, session: Session, step, run_task,
                      max_voltage_mv: int | None, result: dict) -> None:
    """Change 0x37, run ANeg, restore 0x37, run ANeg again. The restore always runs.

    Everything that could make the method pointless or harmful is checked from
    the registers before the first write.
    """
    original = _read(session, 0x37)
    original_bytes = bytes.fromhex(original["data"])
    fields = registers.auto_negotiate_sink(original_bytes)
    before = _contract(session)
    contract = int(before["pdo"], 16)
    contract_mv = before["mv"]
    if contract_mv is None:
        raise BenchRefusedError(f"aneg handles a Fixed Supply contract only and the active PDO is "
                                f"{before['pdo']}; use gsrc")

    offered = registers.source_caps_pdos(bytes.fromhex(_read(session, 0x30)["data"]))
    fixed = [(registers.fixed_pdo_voltage_mv(p), p) for p in offered
             if registers.fixed_pdo_voltage_mv(p) is not None]
    host_min_mv = fields.host_min_voltage_mv  # None while the controller computes the minimum

    if max_voltage_mv is None:
        lower = sorted(v for v, _ in fixed if v < contract_mv)
        if not lower:
            raise BenchRefusedError(f"the contract is at {contract_mv} mV and the source offers no "
                                    "lower Fixed PDO, so a lower maximum cannot change the request; "
                                    "use gsrc")
        reachable = [v for v in lower if host_min_mv is None or v >= host_min_mv]
        if not reachable:
            raise BenchRefusedError(
                f"0x37 holds a host-provided minimum of {host_min_mv} mV (bit 4 is 0) and every "
                f"Fixed PDO below the contract's {contract_mv} mV is under it, so no lower maximum "
                "leaves a PDO to request; use gsrc")
        max_voltage_mv = reachable[-1]
    else:
        if not 5000 <= max_voltage_mv < contract_mv or max_voltage_mv % 50:
            raise BenchRefusedError(f"max_voltage_mv must be a multiple of 50 from 5000 to below the "
                                    f"contract's {contract_mv} mV")
        if host_min_mv is not None and host_min_mv > max_voltage_mv:
            raise BenchRefusedError(
                f"0x37 holds a host-provided minimum of {host_min_mv} mV (bit 4 is 0), above the "
                f"temporary maximum of {max_voltage_mv} mV, so no PDO would fit and ANeg would "
                "change nothing; give a max_voltage_mv of at least the minimum, or use gsrc")

    # The PDO the controller should pick: the most powerful one inside the
    # voltage window (SLVUCR8B p.40, Auto Neg RDO Priority). Only Fixed PDOs
    # are considered here, which is the conservative reading for the check
    # that follows.
    window = [(registers.fixed_pdo_power_mw(p), v, p) for v, p in fixed
              if v <= max_voltage_mv and (host_min_mv is None or v >= host_min_mv)]
    expected_power_mw, expected_mv, expected_pdo = max(window) if window else (0, None, 0)

    # With bit 6 set, a contract below the capability mismatch power makes the
    # controller open the sink path. The temporary value then clears bit 6 too;
    # the restore brings it back with every other bit.
    clear_bit6 = fields.mismatch_disables_sink(expected_power_mw)
    changed = registers.with_host_max_voltage(original_bytes, max_voltage_mv,
                                              keep_sink_on_mismatch=clear_bit6).hex()
    result.update(
        contract_mv=contract_mv, temporary_max_voltage_mv=max_voltage_mv,
        original_0x37=original["data"], temporary_0x37=changed,
        original_contract=before,
        expected_temporary_pdo=f"0x{expected_pdo:08X}" if window else None,
        expected_temporary_power_mw=expected_power_mw if window else None,
        cap_mismatch_power_mw=fields.mismatch_power_mw,
        cleared_bit6=clear_bit6)
    if clear_bit6:
        result["cleared_bit6_reason"] = (
            f"the temporary PDO offers {expected_power_mw} mW, below the capability mismatch power "
            f"of {fields.mismatch_power_mw} mW in 0x37 bits 61:52, and bit 6 (Auto Disable Sink Upon "
            "Capability Mismatch) is set, so the controller would have opened the sink path; bit 6 "
            "is cleared in the temporary value and restored with the rest")

    # From the moment the temporary write is sent, 0x37 may hold the temporary
    # value, even if its reply is lost. The restore therefore runs on every path
    # out of this block.
    try:
        reply = session.link.command(f"WR 0x37 {changed}", WRITE_TIMEOUT_S)
        step("WR 0x37 temporary", reply, match=_register_from(reply).get("match"))
        run_task(session, "ANeg")
        service.sleep(ANEG_SETTLE_S)

        # Did the contract follow? ANeg sends a Request only when the
        # re-evaluated RDO differs, so a contract that stayed means the change
        # to 0x37 did not move the controller's choice.
        after = _contract_or_error(session)
        result["temporary_contract"] = after
        if "error" in after:
            result["stepped_down"] = None
        else:
            result["stepped_down"] = (after["pdo"] != before["pdo"] and after["mv"] is not None
                                      and after["mv"] <= max_voltage_mv)
    finally:
        _restore_0x37(service, session, step, run_task, original["data"], before, result)


def _restore_0x37(service: BenchService, session: Session, step, run_task, original_hex: str,
                  before: dict, result: dict) -> None:
    """Write 0x37 back and run ANeg, whatever happened before. Never raises past a failed restore."""
    restored = False
    failure = None
    for _ in range(2):  # one retry: the restore matters more than the method
        try:
            reply = session.link.command(f"WR 0x37 {original_hex}", WRITE_TIMEOUT_S)
        except (BenchError, serial.SerialException, OSError) as exc:
            failure = str(exc)
            continue
        entry = _register_from(reply)
        step("WR 0x37 restore", reply, match=entry.get("match"))
        restored = entry["data"] == original_hex
        if restored:
            break
        failure = "0x37 read back differently from the original"
    result["restored_0x37"] = restored
    if not restored:
        result["restore_error"] = (f"0x37 was NOT restored ({failure}). Write it back with "
                                   f"bench_write(0x37, \"{original_hex}\"), or restart the "
                                   "controller, which reloads its configuration")
        return
    try:
        run_task(session, "ANeg")
    except (BenchError, serial.SerialException, OSError) as exc:
        step("CMD ANeg", None, error=str(exc))
        result["second_aneg_error"] = (f"0x37 is restored, but the second ANeg failed ({exc}); the "
                                       "contract may still be the temporary one. Run gsrc")

    # Report whether the original contract is back. A failed read is reported,
    # not raised: the register is restored either way.
    service.sleep(ANEG_SETTLE_S)
    final = _contract_or_error(session)
    result["final_contract"] = final
    result["restored_contract"] = None if "error" in final else final["pdo"] == before["pdo"]


def _renegotiate_port_reset(service: BenchService, session: Session, step, result: dict) -> None:
    """Rewrite 0x28 with its own value, then watch STATUS for the disconnect."""
    current = _read(session, 0x28)
    reply = session.link.command(f"WR 0x28 {current['data']}", WRITE_TIMEOUT_S)
    step("WR 0x28 same value", reply, match=_register_from(reply).get("match"))

    # Whether a write of the same value counts as a modification is not in the
    # TRM, so report what STATUS did: Plug Present is bit 0 (SLVUCR8B p.25).
    changes = []
    last: object = "unread"  # plug is True, False, or None for a failed read
    started = service.clock()
    while service.clock() - started < PORT_RESET_WATCH_S:
        try:
            status = bytes.fromhex(_read(session, 0x1A)["data"])
        except BenchCommandError:
            status = b""
        plug = bool(status[0] & 0x01) if status else None
        if plug != last:
            changes.append({"after_s": round(service.clock() - started, 2), "plug": plug})
            last = plug
        service.sleep(PORT_RESET_POLL_S)
    result["status_changes"] = changes
    result["observed_disconnect"] = any(change["plug"] is False for change in changes)


def build_server(tools: SimpleNamespace) -> MCPServer:
    """Register the tools on a new MCP server."""
    server = MCPServer(
        "tps25751-bench",
        version=_package_version(),
        instructions="Tools for a TPS25751 USB PD controller through the bench console firmware: "
                     "raw and decoded register reads, the cross-check snapshot, 4CC tasks, register "
                     "writes, and renegotiation stimuli. bench_task, bench_write, and "
                     "bench_renegotiate can change the PD contract or drop VBUS.",
    )
    read_only = ToolAnnotations(readOnlyHint=True, destructiveHint=False, openWorldHint=False)
    snapshot = ToolAnnotations(readOnlyHint=False, destructiveHint=False, openWorldHint=False)
    changes_board = ToolAnnotations(readOnlyHint=False, destructiveHint=True, openWorldHint=False)
    server.tool(annotations=read_only)(tools.bench_hello)
    server.tool(annotations=snapshot)(tools.bench_snapshot)
    server.tool(annotations=read_only)(tools.bench_read)
    server.tool(annotations=changes_board)(tools.bench_task)
    server.tool(annotations=changes_board)(tools.bench_write)
    server.tool(annotations=changes_board)(tools.bench_renegotiate)
    return server


def _package_version() -> str:
    try:
        from importlib.metadata import version
        return version("tps25751-pd-bench")
    except Exception:  # not installed, for example a bare source checkout
        return "0"


def main() -> None:
    """Console script entry point: serve the tools over stdio."""
    build_server(build_tools(BenchService())).run()


if __name__ == "__main__":
    main()
