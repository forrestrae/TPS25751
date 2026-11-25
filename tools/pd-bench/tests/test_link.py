"""Framing over the serial port: interleaved lines, fragments, errors, and timeouts."""

import pytest
import serial

from fakes import CONSOLE_PORT, SILENT, FakeClock, FakeConsole, FakeSerialFactory
from tps25751_pd_bench.link import BenchLink, TrafficLog
from tps25751_pd_bench.protocol import BenchBusyError, BenchCommandError, BenchPortError, BenchTimeoutError


@pytest.fixture
def rig():
    clock = FakeClock()
    console = FakeConsole()
    factory = FakeSerialFactory(clock, {CONSOLE_PORT: console})

    def link(**kwargs) -> BenchLink:
        return BenchLink(CONSOLE_PORT, serial_factory=factory, clock=clock, now_utc=clock.utc, **kwargs)

    return clock, console, factory, link


def test_request_and_reply(rig):
    clock, console, factory, link = rig
    with link() as bench:
        reply = bench.command("RD 0x34", 3.0)
    assert reply.ok
    assert reply.lines == ["reg=0x34 len=6 data=d64106009800"]
    assert console.requests == ["RD 0x34"]
    # The request went out as one line with an id, and the port was closed again.
    assert factory.written[0] == f"@{reply.id} RD 0x34"
    assert factory.open_now == set()
    assert reply.sent_utc and reply.finished_utc


def test_port_is_opened_exclusively(rig):
    clock, console, factory, link = rig
    with link():
        assert factory.last.kwargs["exclusive"] is True
        assert factory.last.kwargs["baudrate"] == 115200


def test_log_lines_and_events_are_kept_apart_from_the_reply(rig):
    clock, console, factory, link = rig
    # Firmware log output and an event arrive between the reply lines.
    console.noise = ["[bench] sketch says hello", "! PATCH result=Success mode=APP sent=1", "@9999999 OK"]
    with link() as bench:
        reply = bench.command("SNAP", 5.0)

    assert reply.ok
    assert len(reply.lines) == 19                      # uptime, 17 registers, int_clear
    assert all("sketch" not in line for line in reply.lines)
    assert reply.events == ["PATCH result=Success mode=APP sent=1"] * 20
    # A reply line for another request id is a stale answer, not part of this reply.
    assert reply.log.count("[bench] sketch says hello") == 20
    assert reply.log.count("@9999999 OK") == 20


def test_fragmented_bytes_and_line_endings(rig):
    clock, console, factory, link = rig
    factory.chunk = 3                       # the reply arrives three bytes at a time
    with link() as bench:
        crlf = bench.command("RD 0x34", 3.0)
        factory.line_ending = b"\n"         # the native stub and other cores send a bare LF
        lf = bench.command("RD 0x34", 3.0)
    assert crlf.lines == lf.lines == ["reg=0x34 len=6 data=d64106009800"]


def test_err_reply_raises_with_code_and_keeps_partial_lines(rig):
    clock, console, factory, link = rig
    console.script["SNAP"] = [["uptime_ms=1", "reg=0x03 len=4 data=41505020", "reg=0x30 error=I2C",
                               "ERR I2C 1 of 17 registers did not answer"]]
    with link() as bench:
        with pytest.raises(BenchCommandError) as caught:
            bench.command("SNAP", 5.0)
        assert caught.value.code == "I2C"
        assert "1 of 17" in str(caught.value)
        assert caught.value.reply.lines[1] == "reg=0x03 len=4 data=41505020"

        # check=False hands the reply back instead.
        console.script["CMD PBMs"] = [["ERR REFUSED task is not on the allowlist"]]
        reply = bench.command("CMD PBMs", 3.0, check=False)
        assert (reply.ok, reply.error_code) == (False, "REFUSED")


def test_timeout_without_any_answer(rig):
    clock, console, factory, link = rig
    console.script["HELLO"] = [SILENT]
    with link() as bench:
        started = clock.now
        with pytest.raises(BenchTimeoutError) as caught:
            bench.command("HELLO", 3.0)
        assert caught.value.answered is False
        assert 3.0 <= clock.now - started < 3.2


def test_first_line_timeout_is_shorter_than_the_reply_timeout(rig):
    clock, console, factory, link = rig
    console.script["HELLO"] = [SILENT]
    with link() as bench:
        started = clock.now
        with pytest.raises(BenchTimeoutError, match="no reply"):
            bench.command("HELLO", 20.0, first_line_timeout_s=1.5)
        assert 1.5 <= clock.now - started < 1.7


def test_timeout_in_the_middle_of_a_reply(rig):
    clock, console, factory, link = rig
    console.script["RESTART warm"] = [["step=issue task=Gaid status=timeout t_ms=5105"]]  # no final line
    with link() as bench:
        with pytest.raises(BenchTimeoutError, match="got 1 reply lines") as caught:
            bench.command("RESTART warm", 40.0, first_line_timeout_s=1.5)
        assert caught.value.answered is True


def test_stale_input_is_drained_at_open(rig):
    clock, console, factory, link = rig
    factory.pending[CONSOLE_PORT] = b"! READY proto=1\r\n@77 OK\r\n"
    with link() as bench:
        assert bench.stale == ["! READY proto=1", "@77 OK"]
        reply = bench.command("RD 0x34", 3.0)
    assert reply.events == [] and reply.log == []


def test_request_ids_differ_between_requests(rig):
    clock, console, factory, link = rig
    with link() as bench:
        first = bench.command("RD 0x34", 3.0)
        second = bench.command("RD 0x34", 3.0)
    assert first.id != second.id


def test_locked_port_is_reported_as_busy(rig):
    clock, console, factory, link = rig
    factory.locked.add(CONSOLE_PORT)
    with pytest.raises(BenchBusyError, match="locked by another process"):
        link().open()


def test_missing_port(rig):
    clock, console, factory, link = rig
    missing = BenchLink("/dev/cu.nothing", serial_factory=factory, clock=clock)
    with pytest.raises(BenchPortError, match="cannot open /dev/cu.nothing"):
        missing.open()
    with pytest.raises(BenchPortError, match="not open"):
        missing.command("HELLO", 1.0)


def test_write_failure_is_a_port_error(rig):
    clock, console, factory, link = rig
    with link() as bench:
        factory.write_error = True
        with pytest.raises(BenchPortError, match="write to"):
            bench.command("HELLO", 1.0)


def test_read_failure_is_a_port_error(rig):
    clock, console, factory, link = rig
    with link() as bench:
        def unplugged(size=1):
            raise serial.SerialException("device disconnected")
        factory.last.read = unplugged
        with pytest.raises(BenchPortError, match="read from"):
            bench.command("HELLO", 1.0)


def test_traffic_log_records_both_directions(rig, tmp_path):
    clock, console, factory, link = rig
    log = tmp_path / "bench.log"
    console.noise = ["[bench] a log line"]
    with link(traffic=TrafficLog(str(log))) as bench:
        reply = bench.command("RD 0x34", 3.0)
    text = log.read_text().splitlines()
    assert any(line.endswith(f"> @{reply.id} RD 0x34") for line in text)
    assert any(line.endswith(f"< @{reply.id} reg=0x34 len=6 data=d64106009800") for line in text)
    assert any(line.endswith("< [bench] a log line") for line in text)
    assert text[0].endswith(f"# open {CONSOLE_PORT}") and text[-1].endswith(f"# close {CONSOLE_PORT}")


def test_traffic_log_that_cannot_be_written_does_not_fail_the_call(rig, tmp_path):
    clock, console, factory, link = rig
    with link(traffic=TrafficLog(str(tmp_path / "missing-directory" / "bench.log"))) as bench:
        assert bench.command("RD 0x34", 3.0).ok
