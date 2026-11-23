"""Test doubles: a simulated bench console behind a fake serial port.

``FakeConsole`` answers the protocol the way ``TPS25751BenchConsole`` does, from a
small register map, and can be scripted per command. ``FakeSerialFactory`` stands
in for ``serial.Serial``, ``FakeProcesses`` for ``lsof`` and ``ps``, and
``FakeClock`` for time, so no test opens a port or waits.
"""

from __future__ import annotations

from types import SimpleNamespace

import serial

TEENSY_VID = 0x16C0
DUAL_PID = 0x048B
LOG_PORT = "/dev/cu.usbmodem181764301"       # Serial, USB interface 0
CONSOLE_PORT = "/dev/cu.usbmodem181764303"   # SerialUSB1, USB interface 2

SILENT = object()   # a scripted reply that never comes

# The registers of the Apple 140 W bundle (test/fixtures/pd-captures), sink role.
SOURCE_CAPS_140W = bytes.fromhex(
    "042c9181092cd102002cb10400d6410600" + "00" * 36)   # 5, 9, 15, and 20 V Fixed PDOs
ACTIVE_PDO_20V = bytes.fromhex("d64106009800")
ACTIVE_RDO = bytes.fromhex("2cb1844100000000000000002cb18441")
# AUTONEGOTIATE_SINK at its TRM reset value (SLVUCR8B p.40), least significant byte first.
AUTONEG_RESET = bytes.fromhex("000000000000000000000000000000000021919041145076")[::-1]

SNAPSHOT_ORDER = [0x03, 0x14, 0x1A, 0x26, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
                  0x37, 0x3F, 0x40, 0x42, 0x48, 0x69, 0x6A]


def default_registers() -> dict[int, bytes]:
    return {
        0x03: b"APP ",
        0x0F: bytes.fromhex("661109f8"),
        0x14: bytes(11),
        0x1A: bytes.fromhex("0d00000000"),          # plug present, sink
        0x26: bytes(5),
        0x27: bytes.fromhex("050914001c03" + "00" * 8),     # as read on the bench board
        0x28: bytes.fromhex("02002201" + "00" * 13),
        0x30: SOURCE_CAPS_140W,
        0x31: bytes(53),
        0x32: bytes(63),
        0x33: bytes(53),
        0x34: ACTIVE_PDO_20V,
        0x35: ACTIVE_RDO,
        0x37: AUTONEG_RESET,
        0x3F: bytes(2),
        0x40: bytes(4),
        0x42: bytes.fromhex("1a000800"),
        0x48: bytes(25),
        0x69: bytes(4),
        0x6A: bytes(13),
    }


class FakeClock:
    """A clock that only moves when something sleeps or a read times out."""

    def __init__(self, start: float = 1000.0) -> None:
        self.now = start
        self.sleeps: list[float] = []

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds

    def utc(self) -> str:
        return f"2026-11-23T10:00:{self.now - 1000:06.3f}Z"


class FakeConsole:
    """The firmware side of the protocol."""

    def __init__(self) -> None:
        self.registers = default_registers()
        self.proto = 1
        self.role = "sink"
        self.plug = True
        self.mode = "APP"
        self.patch_bundle = False
        self.patch_source = "eeprom"
        self.requests: list[str] = []        # request text, without the id
        self.script: dict[str, list] = {}    # request text -> replies, used in order
        self.noise: list[str] = []           # lines put before every reply line
        self.on_write = None                 # called with (address, bytes) after a WR
        self.decoders = {0x1A: ["=== Status Register (0x1A) ===", "  Plug Present: Yes", ""]}

    # ----- replies -----

    def hello_lines(self) -> list[str]:
        return [
            f"proto={self.proto} lib=3.1.0 build=Nov_23_2026_10:11:12 uptime_ms=123456",
            f"mode={self.mode} version=f8091166 role={self.role} plug={int(self.plug)}",
            f"patch_bundle={int(self.patch_bundle)} patch_source={self.patch_source} swaps=0",
            "OK",
        ]

    def register_line(self, address: int) -> str:
        data = self.registers[address]
        return f"reg=0x{address:02x} len={len(data)} data={data.hex()}"

    def reply_lines(self, text: str):
        scripted = self.script.get(text)
        if scripted:
            reply = scripted.pop(0)
            return reply(self) if callable(reply) else reply

        words = text.split()
        command = words[0]
        if command == "HELLO":
            return self.hello_lines()
        if command in ("RD", "DEC"):
            address = int(words[1], 16)
            if address not in self.registers:
                return ["ERR I2C register read failed"]
            lines = [self.register_line(address)]
            if command == "DEC":
                if address not in self.decoders:
                    return lines + ["ERR NODECODER the library has no register class for this address"]
                lines += [f"| {line}" if line else "|" for line in self.decoders[address]]
            return lines + ["OK"]
        if command == "SNAP":
            return (["uptime_ms=123999"] + [self.register_line(a) for a in SNAPSHOT_ORDER]
                    + ["int_clear=ok", "OK"])
        if command == "WR":
            address = int(words[1], 16)
            if address not in (0x27, 0x28, 0x37, 0x42):
                return ["ERR REFUSED register is not on the write allowlist (0x27, 0x28, 0x37, 0x42)"]
            data = bytes.fromhex(words[2])
            if len(data) != len(self.registers[address]):
                return [f"ERR BADARG the register is {len(self.registers[address])} bytes, got {len(data)}"]
            self.registers[address] = data
            if self.on_write:
                self.on_write(address, data)
            return [self.register_line(address) + " match=1", "OK"]
        if command == "CMD":
            if words[1] in ("PBMs", "I2Cw"):
                return ["ERR REFUSED task is not on the allowlist"]
            return [f"task={words[1]} status=success rc=0x00", "data=" + "00" * 64, "OK"]
        if command == "RESTART":
            task = "GAID" if words[1] == "cold" else "Gaid"
            return [f"step=issue task={task} status=timeout t_ms=5105", "step=nak t_ms=6005",
                    "step=back evidence=data_cleared t_ms=6305", "step=done mode=APP t_ms=6305", "OK"]
        return ["ERR UNKNOWN unknown command"]

    def handle(self, line: str) -> list[str]:
        """The lines the console sends for one request line."""
        request_id, _, text = line[1:].partition(" ")
        self.requests.append(text)
        lines = self.reply_lines(text)
        if lines is SILENT:
            return []
        out: list[str] = []
        for reply in lines:
            out.extend(self.noise)
            out.append(reply if reply.startswith(("!", "[")) else f"@{request_id} {reply}")
        return out


class FakeSerial:
    """A pyserial port wired to a FakeConsole. An empty read costs one read timeout."""

    def __init__(self, port: str, console, clock: FakeClock, factory, **kwargs) -> None:
        self.port = port
        self.kwargs = kwargs
        self._console = console
        self._clock = clock
        self._factory = factory
        self._rx = bytearray(factory.pending.pop(port, b""))
        self._tx = b""
        self.closed = False

    @property
    def in_waiting(self) -> int:
        return len(self._rx)

    def write(self, data: bytes) -> int:
        if self._factory.write_error:
            raise serial.SerialException("write failed: device reports readiness to read but returned no data")
        self._tx += data
        while b"\n" in self._tx:
            line, self._tx = self._tx.split(b"\n", 1)
            self._factory.written.append(line.decode("ascii"))
            if self._console is None:
                continue  # the log port: firmware never reads it
            for reply in self._console.handle(line.decode("ascii")):
                self._rx += reply.encode("utf-8") + self._factory.line_ending
        return len(data)

    def flush(self) -> None:
        pass

    def read(self, size: int = 1) -> bytes:
        if not self._rx:
            self._clock.sleep(self.kwargs.get("timeout") or 0.05)
            return b""
        size = min(size, self._factory.chunk) if self._factory.chunk else size
        chunk = bytes(self._rx[:size])
        del self._rx[:size]
        return chunk

    def close(self) -> None:
        self.closed = True
        self._factory.open_now.discard(self.port)


class FakeSerialFactory:
    """Stands in for ``serial.Serial``. ``consoles`` maps a port to its console, or None for a log port."""

    def __init__(self, clock: FakeClock, consoles: dict) -> None:
        self.clock = clock
        self.consoles = consoles
        self.locked: set[str] = set()        # ports another process holds with flock
        self.pending: dict[str, bytes] = {}  # bytes waiting on a port before it is opened
        self.opened: list[str] = []          # every open, in order
        self.open_now: set[str] = set()
        self.written: list[str] = []         # every request line sent, on any port
        self.last: FakeSerial | None = None
        self.chunk = 0                       # deliver at most this many bytes per read; 0 = all
        self.line_ending = b"\r\n"           # the Teensy core's println()
        self.write_error = False

    def __call__(self, port: str, **kwargs) -> FakeSerial:
        if port not in self.consoles:
            raise serial.SerialException(f"[Errno 2] could not open port {port}: No such file or directory")
        if port in self.locked:
            raise serial.SerialException(f"Could not exclusively lock port {port}: [Errno 35] Resource "
                                         "temporarily unavailable")
        assert port not in self.open_now, "the port was opened twice without a close"
        self.opened.append(port)
        self.open_now.add(port)
        self.last = FakeSerial(port, self.consoles[port], self.clock, self, **kwargs)
        return self.last


def dual_serial_ports(serial_number: str = "18176430", location: str | None = "20-2") -> list:
    """What pyserial lists for a Teensy running examples/pd-bench on macOS, plus the KM003C."""
    return [
        SimpleNamespace(device="/dev/cu.usbmodem0538932", vid=0x5FC9, pid=0x0063,
                        serial_number="053893", location="20-1"),
        SimpleNamespace(device=f"/dev/cu.usbmodem{serial_number}1", vid=TEENSY_VID, pid=DUAL_PID,
                        serial_number=serial_number, location=location),
        SimpleNamespace(device=f"/dev/cu.usbmodem{serial_number}3", vid=TEENSY_VID, pid=DUAL_PID,
                        serial_number=serial_number, location=location),
    ]


class FakeProcesses:
    """Stands in for ``subprocess.run`` on ``lsof`` and ``ps``."""

    def __init__(self) -> None:
        self.holders: dict[str, list[tuple[int, str, str]]] = {}  # device -> (pid, name, command line)
        self.calls: list[list[str]] = []
        self.lsof_missing = False

    def __call__(self, args, **kwargs):
        self.calls.append(list(args))
        if args[0] == "lsof":
            if self.lsof_missing:
                raise FileNotFoundError("lsof")
            fields = "".join(f"p{pid}\nc{name}\n" for pid, name, _ in self.holders.get(args[-1], []))
            return SimpleNamespace(stdout=fields, returncode=0 if fields else 1)
        if args[0] == "ps":
            pid = int(args[-1])
            for entries in self.holders.values():
                for holder_pid, _, command_line in entries:
                    if holder_pid == pid:
                        return SimpleNamespace(stdout=command_line + "\n", returncode=0)
            return SimpleNamespace(stdout="", returncode=1)
        raise AssertionError(f"unexpected command {args}")
