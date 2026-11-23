"""One open serial connection to the bench console.

A ``BenchLink`` lives for a single tool call: it opens the port with an exclusive
lock, exchanges request and reply lines, and closes. Nothing holds the port
between calls, so ``pio run -t upload`` and a serial monitor on the log port work
at any time.
"""

from __future__ import annotations

import itertools
import os
import time
from datetime import datetime, timezone
from typing import Callable

import serial

from .protocol import (
    BenchBusyError,
    BenchCommandError,
    BenchPortError,
    BenchTimeoutError,
    Reply,
    classify,
    final_status,
    format_request,
)

BAUD_RATE = 115200          # ignored by USB CDC, required by pyserial
READ_TIMEOUT_S = 0.05       # one blocking read; the reply deadline is checked between reads
WRITE_TIMEOUT_S = 2.0
DRAIN_S = 0.05              # how long open() collects stale input
FIRST_LINE_TIMEOUT_S = 1.5  # a console answers HELLO's first line without touching I2C

_request_ids = itertools.count(int.from_bytes(os.urandom(2), "big") % 9000 + 1000)


def utc_now() -> str:
    """Current time as ISO 8601 UTC with milliseconds, the form the capture tools use."""
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


class TrafficLog:
    """Appends every line sent and received to the file named by TPS25751_BENCH_LOG."""

    def __init__(self, path: str | None) -> None:
        self.path = path

    def write(self, direction: str, text: str) -> None:
        if not self.path:
            return
        try:
            with open(self.path, "a", encoding="utf-8") as handle:
                handle.write(f"{utc_now()} {direction} {text}\n")
        except OSError:
            pass  # the log is a convenience; a full disk must not fail a tool call


class BenchLink:
    """A request and reply channel over one open serial port."""

    def __init__(self, port: str, *, serial_factory: Callable[..., object] = serial.Serial,
                 traffic: TrafficLog | None = None,
                 clock: Callable[[], float] = time.monotonic,
                 now_utc: Callable[[], str] = utc_now) -> None:
        self.port = port
        self._factory = serial_factory
        self._traffic = traffic or TrafficLog(None)
        self._clock = clock
        self._now_utc = now_utc
        self._serial = None
        self._pending = b""
        self.stale: list[str] = []  # lines that were waiting when the port opened

    # ----- lifetime -----

    def open(self) -> "BenchLink":
        """Open the port exclusively and discard input left from before."""
        try:
            self._serial = self._factory(self.port, baudrate=BAUD_RATE, timeout=READ_TIMEOUT_S,
                                         write_timeout=WRITE_TIMEOUT_S, exclusive=True)
        except serial.SerialException as exc:
            text = str(exc)
            if "exclusively lock" in text or "Resource busy" in text or "Errno 16" in text:
                raise BenchBusyError(f"{self.port} is locked by another process: {text}") from exc
            raise BenchPortError(f"cannot open {self.port}: {text}") from exc
        self._traffic.write("#", f"open {self.port}")
        deadline = self._clock() + DRAIN_S
        while self._clock() < deadline:
            for line in self._read_lines():
                self.stale.append(line)
                self._traffic.write("<", f"(stale) {line}")
        return self

    def close(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            except (OSError, serial.SerialException):
                pass
            self._serial = None
            self._traffic.write("#", f"close {self.port}")

    def __enter__(self) -> "BenchLink":
        return self.open()

    def __exit__(self, *exc_info) -> None:
        self.close()

    # ----- requests -----

    def command(self, command: str, timeout_s: float, *,
                first_line_timeout_s: float | None = None, check: bool = True) -> Reply:
        """Send one request and collect its reply.

        Lines for other request ids are stale replies and are kept as log lines.
        ``first_line_timeout_s`` bounds the wait for the first reply line, which
        tells a port that is not the console from a console that is busy.
        Raises ``BenchCommandError`` for an ``ERR`` reply unless ``check`` is false.
        """
        if self._serial is None:
            raise BenchPortError("the console port is not open")
        request_id = str(next(_request_ids))
        reply = Reply(request=command, id=request_id)
        payload = format_request(request_id, command)
        try:
            self._serial.write(payload)
            self._serial.flush()
        except (OSError, serial.SerialException) as exc:
            raise BenchPortError(f"write to {self.port} failed: {exc}") from exc
        reply.sent_utc = self._now_utc()
        self._traffic.write(">", payload.decode("ascii").rstrip("\n"))

        started = self._clock()
        deadline = started + timeout_s
        first_deadline = started + first_line_timeout_s if first_line_timeout_s else None
        answered = False
        while True:
            for raw in self._read_lines():
                self._traffic.write("<", raw)
                line = classify(raw)
                if line.kind == "event":
                    reply.events.append(line.text)
                    continue
                if line.kind == "log" or line.id != request_id:
                    reply.log.append(raw)
                    continue
                answered = True
                status = final_status(line.text)
                if status is None:
                    reply.lines.append(line.text)
                    continue
                reply.ok, reply.error_code, reply.error_text = status
                reply.finished_utc = self._now_utc()
                if check and not reply.ok:
                    raise BenchCommandError(reply)
                return reply
            now = self._clock()
            if not answered and first_deadline is not None and now >= first_deadline:
                raise BenchTimeoutError(
                    f"no reply from {self.port} to '{command}' within {first_line_timeout_s:g} s",
                    answered=False)
            if now >= deadline:
                seen = f"; got {len(reply.lines)} reply lines" if answered else ""
                raise BenchTimeoutError(
                    f"the console on {self.port} did not finish '{command}' within {timeout_s:g} s{seen}",
                    answered=answered)

    # ----- internals -----

    def _read_lines(self) -> list[str]:
        """Read what is available (blocking up to the read timeout) and return whole lines."""
        try:
            waiting = getattr(self._serial, "in_waiting", 0) or 1
            chunk = self._serial.read(waiting)
        except (OSError, serial.SerialException) as exc:
            raise BenchPortError(f"read from {self.port} failed: {exc}") from exc
        if not chunk:
            return []
        self._pending += chunk
        *whole, self._pending = self._pending.split(b"\n")
        return [part.decode("utf-8", errors="replace").rstrip("\r") for part in whole]
