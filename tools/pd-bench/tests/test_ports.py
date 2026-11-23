"""Port discovery and the lsof holder check."""

from types import SimpleNamespace

import pytest

from fakes import CONSOLE_PORT, DUAL_PID, LOG_PORT, TEENSY_VID, FakeProcesses, dual_serial_ports
from tps25751_pd_bench import ports
from tps25751_pd_bench.protocol import BenchPortError


def find(listing, **kwargs):
    return ports.find_candidates(comports=lambda: listing, **kwargs)


def test_dual_serial_on_macos_puts_the_second_interface_first():
    # pyserial gives both ports the same location and no interface number on
    # macOS; the console (USB interface 2) is the device name that sorts last.
    candidates = find(dual_serial_ports())
    assert [c.device for c in candidates] == [CONSOLE_PORT, LOG_PORT]
    assert candidates[0].designated and not candidates[1].designated
    assert candidates[0].pid == 0x048B


def test_other_usb_devices_are_ignored():
    # The KM003C's trigger port (VID 0x5FC9) is in the listing and never a candidate.
    assert all("0538932" not in c.device for c in find(dual_serial_ports()))


def test_dual_serial_on_linux_uses_the_interface_number():
    listing = [
        SimpleNamespace(device="/dev/ttyACM1", vid=TEENSY_VID, pid=DUAL_PID,
                        serial_number="18176430", location="1-1.2:1.0"),
        SimpleNamespace(device="/dev/ttyACM0", vid=TEENSY_VID, pid=DUAL_PID,
                        serial_number="18176430", location="1-1.2:1.2"),
    ]
    # ttyACM0 is on interface 2 here, so the name order would pick the wrong port.
    assert [c.device for c in find(listing)] == ["/dev/ttyACM0", "/dev/ttyACM1"]
    assert ports.interface_number("1-1.2:1.2") == 2
    assert ports.interface_number("20-2") is None
    assert ports.interface_number(None) is None


def test_single_serial_teensy_is_a_candidate_without_being_designated():
    # Firmware that embeds the console on Serial enumerates as PID 0x0483.
    listing = [SimpleNamespace(device="/dev/cu.usbmodem181764301", vid=TEENSY_VID, pid=0x0483,
                               serial_number="18176430", location="20-2")]
    candidates = find(listing)
    assert [c.device for c in candidates] == ["/dev/cu.usbmodem181764301"]
    assert not candidates[0].designated


def test_explicit_port_skips_discovery():
    def never():
        raise AssertionError("discovery ran although a port was given")

    candidates = ports.find_candidates(port="/dev/cu.custom", comports=never)
    assert [(c.device, c.designated) for c in candidates] == [("/dev/cu.custom", True)]


def test_serial_number_narrows_to_one_board():
    listing = dual_serial_ports("18176430") + dual_serial_ports("99990000")[1:]
    with pytest.raises(BenchPortError, match="more than one Teensy.*18176430, 99990000"):
        find(listing)

    chosen = find(listing, serial_number="99990000")
    assert [c.device for c in chosen] == ["/dev/cu.usbmodem999900003", "/dev/cu.usbmodem999900001"]

    with pytest.raises(BenchPortError, match="serial number 12345678"):
        find(listing, serial_number="12345678")


def test_no_teensy_attached():
    with pytest.raises(BenchPortError, match="no Teensy serial port found"):
        find(dual_serial_ports()[:1])


def test_candidate_description():
    candidate = find(dual_serial_ports())[0]
    assert candidate.describe() == f"{CONSOLE_PORT}, PID 0x048b, serial 18176430"
    assert ports.PortCandidate(device="/dev/x").describe() == "/dev/x"


def test_holders_are_named_with_their_command_line():
    processes = FakeProcesses()
    processes.holders[CONSOLE_PORT] = [(4321, "python3.1", "python3 -m platformio device monitor")]
    check = ports.port_holders(CONSOLE_PORT, run=processes, own_pid=1)
    assert check.holders == ("pid 4321 (python3 -m platformio device monitor)",)
    assert check.unavailable is None


def test_own_process_is_not_a_holder():
    processes = FakeProcesses()
    processes.holders[CONSOLE_PORT] = [(77, "python3.1", "tps25751-pd-bench")]
    check = ports.port_holders(CONSOLE_PORT, run=processes, own_pid=77)
    assert check.holders == () and check.unavailable is None


def test_macos_dial_in_twin_counts_as_the_same_port():
    # A program that opened /dev/tty.usbmodem... holds the port /dev/cu.usbmodem... names.
    processes = FakeProcesses()
    twin = CONSOLE_PORT.replace("/dev/cu.", "/dev/tty.")
    processes.holders[twin] = [(55, "screen", "screen " + twin)]
    assert ports.port_holders(CONSOLE_PORT, run=processes, own_pid=1).holders == (f"pid 55 (screen {twin})",)
    assert [call[-1] for call in processes.calls if call[0] == "lsof"] == [CONSOLE_PORT, twin]

    # And the other way round; a Linux name has no twin.
    processes.calls.clear()
    ports.port_holders(twin, run=processes, own_pid=1)
    assert [call[-1] for call in processes.calls if call[0] == "lsof"] == [twin, CONSOLE_PORT]
    processes.calls.clear()
    ports.port_holders("/dev/ttyACM0", run=processes, own_pid=1)
    assert [call[-1] for call in processes.calls if call[0] == "lsof"] == ["/dev/ttyACM0"]


def test_holder_falls_back_to_the_short_name_when_ps_knows_nothing():
    def run(args, **kwargs):
        if args[0] == "lsof":
            return SimpleNamespace(stdout="p4321\ncminicom\n", returncode=0)
        raise OSError("ps is not available")

    assert ports.port_holders("/dev/ttyACM0", run=run, own_pid=1).holders == ("pid 4321 (minicom)",)


def test_a_check_that_cannot_run_says_so():
    # A missing or hanging lsof must not look like "nobody holds the port".
    processes = FakeProcesses()
    processes.holders[CONSOLE_PORT] = [(4321, "python3.1", "pio device monitor")]

    processes.lsof_missing = True
    check = ports.port_holders(CONSOLE_PORT, run=processes, own_pid=1)
    assert check.holders == () and check.unavailable == "lsof is not installed"

    processes.lsof_missing, processes.lsof_hangs = False, True
    check = ports.port_holders(CONSOLE_PORT, run=processes, own_pid=1)
    assert check.unavailable == "lsof did not finish within 5 s"

    def denied(args, **kwargs):
        raise PermissionError("permission denied")

    check = ports.port_holders(CONSOLE_PORT, run=denied, own_pid=1)
    assert check.unavailable == "lsof could not run: permission denied"
