"""Framing of the bench console's line protocol, version 1.

The firmware side is ``TPS25751BenchProtocol`` in the library::

    request  @<id> <COMMAND> [arguments]
    reply    @<id> <text>                 zero or more lines
    final    @<id> OK
             @<id> ERR <CODE> <text>
    event    ! <text>
    log      anything else

Reply text is space-separated ``key=value`` tokens, or ``| `` followed by one line
of a register's debugPrint() output. This module only frames and parses lines;
``link.py`` moves them over the serial port.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from . import PROTOCOL_VERSION

REPLY_LINE = re.compile(r"^@([0-9A-Za-z?]{1,8})(?: (.*))?$")
MAX_ID_LENGTH = 8


class BenchError(Exception):
    """Base class of the errors a tool call reports as ``{"error", "kind"}``."""


class BenchPortError(BenchError):
    """No console port was found, or the one found could not be opened."""


class BenchBusyError(BenchError):
    """Another process holds the console's serial port."""


class BenchTimeoutError(BenchError):
    """The console did not finish its reply in time."""

    def __init__(self, message: str, *, answered: bool = False) -> None:
        super().__init__(message)
        self.answered = answered  # True if at least one reply line arrived


class BenchProtocolError(BenchError):
    """The console speaks another protocol version or sent a malformed reply."""


class BenchCommandError(BenchError):
    """The console answered ``ERR``."""

    def __init__(self, reply: "Reply") -> None:
        super().__init__(f"console answered ERR {reply.error_code}: {reply.error_text}")
        self.reply = reply
        self.code = reply.error_code


class BenchRefusedError(BenchError):
    """The server refused the call before sending anything that changes the board."""


@dataclass
class Reply:
    """Everything that arrived between a request and its final line."""

    request: str
    id: str
    ok: bool = False
    lines: list[str] = field(default_factory=list)   # reply text, without the "@<id> " prefix
    error_code: str | None = None
    error_text: str | None = None
    log: list[str] = field(default_factory=list)     # lines that are not protocol lines
    events: list[str] = field(default_factory=list)  # text of "! ..." lines
    sent_utc: str | None = None
    finished_utc: str | None = None

    def tokens(self) -> dict[str, str]:
        """All ``key=value`` tokens of the reply's data lines; a later line wins."""
        out: dict[str, str] = {}
        for line in self.data_lines():
            out.update(parse_tokens(line))
        return out

    def data_lines(self) -> list[str]:
        """Reply lines that are tokens, not decode text."""
        return [line for line in self.lines if not is_decode_line(line)]

    def decode_text(self) -> list[str]:
        """The debugPrint() lines of a ``DEC`` reply, without the ``| `` marker."""
        return [line[2:] if line.startswith("| ") else "" for line in self.lines
                if is_decode_line(line)]

    def extras(self) -> dict:
        """Log lines and events to pass on with a tool result, when there are any."""
        out: dict = {}
        if self.log:
            out["log"] = self.log[-20:]
        if self.events:
            out["events"] = self.events[-20:]
        return out


def is_decode_line(line: str) -> bool:
    return line == "|" or line.startswith("| ")


def parse_tokens(line: str) -> dict[str, str]:
    """Split ``reg=0x34 len=6 data=d641`` into a dict. Tokens without ``=`` are dropped."""
    out: dict[str, str] = {}
    for token in line.split():
        key, sep, value = token.partition("=")
        if sep and key:
            out[key] = value
    return out


def format_request(request_id: str, command: str) -> bytes:
    """One request line, with the line feed."""
    if not re.fullmatch(r"[0-9A-Za-z]{1,8}", request_id):
        raise ValueError(f"request id {request_id!r} is not 1 to 8 letters or digits")
    if "\n" in command or "\r" in command:
        raise ValueError("a request is a single line")
    return f"@{request_id} {command}\n".encode("ascii")


@dataclass
class Line:
    """One received line, classified."""

    kind: str            # "reply", "event", or "log"
    text: str            # reply text, event text, or the whole log line
    id: str | None = None


def classify(raw: str) -> Line:
    """Sort a received line into a reply line, an event, or log output."""
    line = raw.rstrip("\r\n")
    match = REPLY_LINE.match(line)
    if match:
        return Line("reply", match.group(2) or "", match.group(1))
    if line == "!" or line.startswith("! "):
        return Line("event", line[2:])
    return Line("log", line)


def final_status(text: str) -> tuple[bool, str | None, str | None] | None:
    """``(ok, code, text)`` if ``text`` is a final line, else None."""
    if text == "OK":
        return True, None, None
    if text == "ERR" or text.startswith("ERR "):
        parts = text.split(" ", 2)
        code = parts[1] if len(parts) > 1 else "UNKNOWN"
        return False, code, parts[2] if len(parts) > 2 else ""
    return None


def check_version(tokens: dict[str, str]) -> None:
    """Raise unless a HELLO reply reports this server's protocol version."""
    proto = tokens.get("proto")
    if proto is None:
        raise BenchProtocolError("the HELLO reply has no proto= token; is this the bench console's port?")
    if proto != str(PROTOCOL_VERSION):
        raise BenchProtocolError(
            f"the console speaks protocol version {proto} and this server speaks version "
            f"{PROTOCOL_VERSION}: flash the firmware and run the server from the same library tag")


def parse_address(addr: int | str) -> int:
    """A register address from an int or a string such as ``"0x34"``.

    A string follows Python's integer syntax, so ``"0x34"`` is hex and ``"52"`` is
    decimal; a hex address needs its prefix.
    """
    if isinstance(addr, bool):
        raise ValueError("a register address is a number from 0x00 to 0xFF")
    if isinstance(addr, int):
        value = addr
    else:
        try:
            value = int(str(addr).strip(), 0)
        except ValueError:
            raise ValueError(f"cannot read {addr!r} as a register address; write it like 0x34") from None
    if not 0 <= value <= 0xFF:
        raise ValueError(f"register address {value:#x} is outside 0x00 to 0xFF")
    return value


def clean_hex(text: str, what: str = "hex") -> str:
    """Lowercase hex without spaces, underscores, or a 0x prefix; raises on anything else."""
    cleaned = re.sub(r"[\s_]", "", text)
    if cleaned.lower().startswith("0x"):
        cleaned = cleaned[2:]
    if len(cleaned) % 2 or not re.fullmatch(r"[0-9a-fA-F]*", cleaned):
        raise ValueError(f"{what} must be an even number of hex digits, got {text!r}")
    return cleaned.lower()
