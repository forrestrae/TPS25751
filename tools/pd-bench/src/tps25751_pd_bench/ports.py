"""Finding the bench console's serial port, and who holds it.

``examples/pd-bench`` builds with ``-D USB_DUAL_SERIAL``, so the Teensy enumerates
as VID 0x16C0, PID 0x048B with two CDC ports: ``Serial`` (log) on USB interface 0
and ``SerialUSB1`` (console) on interface 2. A single-serial Teensy is PID 0x0483.

pyserial reports no interface number on macOS, so the two ports of one board
differ only in their device name there: macOS names a CDC port
``/dev/cu.usbmodem<serial><n>`` with ``n`` one more than the interface number, so
the console is the name that sorts last. On Linux the ``location`` string ends in
the interface number. Either way the choice is confirmed with ``HELLO`` before it
is used, and ``TPS25751_BENCH_PORT`` overrides all of it.
"""

from __future__ import annotations

import os
import re
import subprocess
from dataclasses import dataclass
from typing import Callable, Iterable

from serial.tools import list_ports

from .protocol import BenchPortError

TEENSY_VID = 0x16C0
DUAL_SERIAL_PID = 0x048B     # teensy4/usb_desc.h, USB_DUAL_SERIAL
SINGLE_SERIAL_PID = 0x0483   # teensy4/usb_desc.h, USB_SERIAL

LSOF_TIMEOUT_S = 5.0


@dataclass(frozen=True)
class PortCandidate:
    device: str
    pid: int | None = None
    serial_number: str | None = None
    location: str | None = None
    designated: bool = False  # True when this port must be the console: no falling back to another

    def describe(self) -> str:
        serial = f", serial {self.serial_number}" if self.serial_number else ""
        pid = f", PID {self.pid:#06x}" if self.pid is not None else ""
        return f"{self.device}{pid}{serial}"


def interface_number(location: str | None) -> int | None:
    """The USB interface number from a Linux location such as ``1-1.2:1.2``; None elsewhere."""
    match = re.search(r":\d+\.(\d+)$", location or "")
    return int(match.group(1)) if match else None


def _console_order(candidate: PortCandidate) -> tuple:
    interface = interface_number(candidate.location)
    return (interface if interface is not None else -1, candidate.device)


def find_candidates(*, port: str | None = None, serial_number: str | None = None,
                    comports: Callable[[], Iterable] = list_ports.comports) -> list[PortCandidate]:
    """Ports to try, most likely console first.

    ``port`` (TPS25751_BENCH_PORT) names the console outright. Otherwise the
    Teensy ports are narrowed by ``serial_number`` (TPS25751_BENCH_SERIAL); more
    than one board left is an error, because guessing could address the wrong
    hardware.
    """
    if port:
        return [PortCandidate(device=port, designated=True)]

    teensy = [PortCandidate(device=p.device, pid=p.pid, serial_number=p.serial_number,
                            location=getattr(p, "location", None))
              for p in comports() if p.vid == TEENSY_VID]
    if serial_number:
        teensy = [c for c in teensy if c.serial_number == serial_number]
        if not teensy:
            raise BenchPortError(
                f"no Teensy serial port with serial number {serial_number} (TPS25751_BENCH_SERIAL)")
    if not teensy:
        raise BenchPortError(
            "no Teensy serial port found (VID 0x16C0). Flash examples/pd-bench, or name the "
            "console's port in TPS25751_BENCH_PORT")

    boards = sorted({c.serial_number or "" for c in teensy})
    if len(boards) > 1:
        raise BenchPortError(
            "more than one Teensy is attached (serial numbers " + ", ".join(b or "unknown" for b in boards)
            + "). Set TPS25751_BENCH_SERIAL to one of them, or TPS25751_BENCH_PORT to the console's port")

    ordered = sorted(teensy, key=_console_order, reverse=True)
    if ordered[0].pid == DUAL_SERIAL_PID and len(ordered) == 2:
        # The pd-bench layout: the console is the second interface. Only if it
        # stays silent is the first interface tried.
        first = ordered[0]
        ordered[0] = PortCandidate(first.device, first.pid, first.serial_number, first.location,
                                   designated=True)
    return ordered


def _sibling_devices(device: str) -> list[str]:
    """The device and, on macOS, its callout or dial-in twin, which is the same port."""
    names = [device]
    base = os.path.basename(device)
    directory = os.path.dirname(device)
    if base.startswith("cu."):
        names.append(os.path.join(directory, "tty." + base[3:]))
    elif base.startswith("tty.") and directory == "/dev":
        names.append(os.path.join(directory, "cu." + base[4:]))
    return names


def _command_line(pid: int, run: Callable[..., object]) -> str | None:
    try:
        result = run(["ps", "-o", "command=", "-p", str(pid)], capture_output=True, text=True,
                     timeout=LSOF_TIMEOUT_S)
    except (OSError, subprocess.SubprocessError):
        return None
    text = (getattr(result, "stdout", "") or "").strip()
    return text[:120] if text else None


def port_holders(device: str, *, run: Callable[..., object] = subprocess.run,
                 own_pid: int | None = None) -> list[str]:
    """Other processes that have ``device`` open, as ``pid 123 (command line)``.

    pyserial's exclusive open is an advisory ``flock`` that a serial monitor does
    not take, so ``lsof`` is what tells that another program is reading the port.
    Without ``lsof`` the list is empty and the exclusive open is the only guard.
    """
    own_pid = os.getpid() if own_pid is None else own_pid
    holders: dict[int, str] = {}
    for name in _sibling_devices(device):
        try:
            result = run(["lsof", "-F", "pc", "--", name], capture_output=True, text=True,
                         timeout=LSOF_TIMEOUT_S)
        except (OSError, subprocess.SubprocessError):
            return []
        pid = None
        for field in (getattr(result, "stdout", "") or "").splitlines():
            if field.startswith("p") and field[1:].isdigit():
                pid = int(field[1:])
            elif field.startswith("c") and pid is not None and pid != own_pid:
                holders.setdefault(pid, field[1:])
    described = []
    for pid, command in sorted(holders.items()):
        full = _command_line(pid, run)
        described.append(f"pid {pid} ({full or command})")
    return described
