"""Discovery, the single-owner rule, and the HELLO handshake, through the tools."""

from fakes import CONSOLE_PORT, LOG_PORT, SILENT, FakeConsole
from conftest import Bench


def hello_count(bench) -> int:
    return bench.requests.count("HELLO")


def test_first_call_greets_and_later_calls_do_not(bench):
    assert bench.tools.bench_read("0x34")["data"] == "d64106009800"
    assert bench.requests == ["HELLO", "RD 0x34"]

    bench.tools.bench_read("0x35")
    bench.tools.bench_snapshot()
    assert hello_count(bench) == 1
    # Only the console port was ever opened: once per call.
    assert bench.factory.opened == [CONSOLE_PORT] * 3


def test_nothing_holds_the_port_between_calls(bench):
    bench.tools.bench_read("0x34")
    assert bench.factory.open_now == set()
    assert bench.factory.last.closed

    bench.console.script["RD 0x34"] = [SILENT]
    assert "error" in bench.tools.bench_read("0x34")
    assert bench.factory.open_now == set()


def test_each_call_checks_for_another_owner_first(bench):
    bench.tools.bench_read("0x34")
    bench.tools.bench_read("0x34")
    lsof = [call for call in bench.processes.calls if call[0] == "lsof"]
    assert [call[-1] for call in lsof if call[-1] == CONSOLE_PORT] == [CONSOLE_PORT] * 2


def test_busy_console_port_names_the_holder(bench):
    bench.processes.holders[CONSOLE_PORT] = [(4321, "python3.1", "python3 -m platformio device monitor")]
    result = bench.tools.bench_hello()

    assert result["kind"] == "BenchBusyError"
    assert CONSOLE_PORT in result["error"]
    assert "pid 4321 (python3 -m platformio device monitor)" in result["error"]
    # Neither the held port nor the log port was opened or written to.
    assert bench.factory.opened == []
    assert bench.factory.written == []


def test_a_monitor_on_the_log_port_does_not_matter(bench):
    bench.processes.holders[LOG_PORT] = [(4321, "python3.1", "pio device monitor")]
    assert bench.tools.bench_hello()["mode"] == "APP"
    assert bench.factory.opened == [CONSOLE_PORT]


def test_busy_port_found_on_a_later_call(bench):
    bench.tools.bench_read("0x34")
    bench.processes.holders[CONSOLE_PORT] = [(99, "screen", "screen " + CONSOLE_PORT)]
    result = bench.tools.bench_read("0x34")
    assert result["kind"] == "BenchBusyError" and "pid 99 (screen" in result["error"]
    assert bench.factory.opened == [CONSOLE_PORT]

    # Once the other program is gone the same port works again, without a new HELLO.
    bench.processes.holders.clear()
    assert bench.tools.bench_read("0x34")["data"] == "d64106009800"
    assert hello_count(bench) == 1


def test_port_locked_by_flock_is_busy(bench):
    bench.factory.locked.add(CONSOLE_PORT)
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchBusyError"
    assert "locked by another process" in result["error"]
    assert bench.factory.opened == []


def test_protocol_version_mismatch_is_refused(bench):
    bench.console.proto = 2
    result = bench.tools.bench_read("0x34")
    assert result["kind"] == "BenchProtocolError"
    assert "protocol version 2 and this server speaks version 1" in result["error"]
    # The register read was never sent, and the port is closed.
    assert bench.requests == ["HELLO"]
    assert bench.factory.open_now == set()

    # Flashing matching firmware fixes it: the next call greets again.
    bench.console.proto = 1
    assert bench.tools.bench_read("0x34")["data"] == "d64106009800"
    assert hello_count(bench) == 2


def test_a_port_that_is_not_the_console_is_refused(bench):
    bench.console.script["HELLO"] = [["something=else", "OK"]]
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchProtocolError" and "no proto= token" in result["error"]


def test_silent_console_port_falls_back_to_the_other_interface():
    # Firmware with the console on Serial and logs on SerialUSB1: the port the
    # layout points to stays silent, so the other one is probed.
    bench = Bench()
    bench.factory.consoles = {LOG_PORT: bench.console, CONSOLE_PORT: None}
    result = bench.tools.bench_hello()
    assert result["port"] == LOG_PORT
    assert bench.factory.opened == [CONSOLE_PORT, LOG_PORT]
    # The probe of the silent port waited for the first line only, not the full HELLO time.
    assert bench.clock.now - 1000 < 3

    # The confirmed port is remembered.
    bench.tools.bench_read("0x34")
    assert bench.factory.opened[-1] == LOG_PORT and len(bench.factory.opened) == 3


def test_no_console_answers(bench):
    bench.factory.consoles = {LOG_PORT: None, CONSOLE_PORT: None}
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchPortError"
    assert f"{CONSOLE_PORT} did not answer HELLO" in result["error"]
    assert f"{LOG_PORT} did not answer HELLO" in result["error"]


def test_silent_console_and_busy_other_port(bench):
    # The designated port is silent and the other is held: it is not probed, and
    # the error says both.
    bench.factory.consoles = {LOG_PORT: None, CONSOLE_PORT: None}
    bench.processes.holders[LOG_PORT] = [(4321, "python3.1", "pio device monitor")]
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchPortError"
    assert "did not answer HELLO" in result["error"] and "pid 4321" in result["error"]
    assert bench.factory.opened == [CONSOLE_PORT]


def test_single_serial_board_that_is_busy():
    bench = Bench()
    bench.ports = bench.ports[1:2]  # one Teensy port, no designated console
    for port in bench.ports:
        port.pid = 0x0483
    bench.processes.holders[LOG_PORT] = [(4321, "python3.1", "pio device monitor")]
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchBusyError" and "pid 4321 (pio device monitor)" in result["error"]


def test_port_override_from_the_environment():
    bench = Bench(env={"TPS25751_BENCH_PORT": "/dev/cu.custom"})
    bench.factory.consoles["/dev/cu.custom"] = FakeConsole()
    bench.ports = []  # discovery would find nothing
    assert bench.tools.bench_hello()["port"] == "/dev/cu.custom"
    assert bench.factory.opened == ["/dev/cu.custom"]


def test_serial_filter_from_the_environment():
    bench = Bench(env={"TPS25751_BENCH_SERIAL": "99990000"})
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchPortError" and "99990000" in result["error"]


def test_no_teensy(bench):
    bench.ports = bench.ports[:1]
    result = bench.tools.bench_hello()
    assert result["kind"] == "BenchPortError" and "no Teensy serial port found" in result["error"]


def test_remembered_port_that_disappears_is_rediscovered(bench):
    bench.tools.bench_read("0x34")
    # The board re-enumerates under another name, as after a different USB port.
    moved = "/dev/cu.usbmodem181764307"
    bench.factory.consoles = {LOG_PORT: None, moved: bench.console}
    bench.ports[2].device = moved

    assert bench.tools.bench_read("0x34")["data"] == "d64106009800"
    assert bench.factory.opened[-1] == moved
    assert hello_count(bench) == 2


def test_a_timeout_makes_the_next_call_greet_again(bench):
    bench.tools.bench_read("0x34")
    bench.console.script["RD 0x34"] = [SILENT]
    result = bench.tools.bench_read("0x34")
    assert result["kind"] == "BenchTimeoutError"

    bench.tools.bench_read("0x34")
    assert hello_count(bench) == 2


def test_traffic_log_from_the_environment(tmp_path):
    log = tmp_path / "bench.log"
    bench = Bench(env={"TPS25751_BENCH_LOG": str(log)})
    bench.tools.bench_read("0x34")
    text = log.read_text()
    assert " HELLO" in text and "RD 0x34" in text and "reg=0x34 len=6 data=d64106009800" in text
