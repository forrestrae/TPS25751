import pytest
import serial

from fakes import CONSOLE_PORT, LOG_PORT, FakeClock, FakeConsole, FakeProcesses, FakeSerialFactory, dual_serial_ports
from tps25751_pd_bench.server import BenchService, build_tools


class Bench:
    """A service wired to a fake Teensy with the pd-bench firmware."""

    def __init__(self, env: dict | None = None) -> None:
        self.clock = FakeClock()
        self.console = FakeConsole()
        self.factory = FakeSerialFactory(self.clock, {LOG_PORT: None, CONSOLE_PORT: self.console})
        self.processes = FakeProcesses()
        self.ports = dual_serial_ports()
        self.env = env or {}
        self.service = self.make_service()
        self.tools = build_tools(self.service)

    def make_service(self) -> BenchService:
        return BenchService(env=self.env, serial_factory=self.factory, comports=lambda: self.ports,
                            run=self.processes, clock=self.clock, sleep=self.clock.sleep,
                            now_utc=self.clock.utc)

    @property
    def requests(self) -> list[str]:
        return self.console.requests


@pytest.fixture
def bench() -> Bench:
    return Bench()


@pytest.fixture(autouse=True)
def no_real_serial_port(monkeypatch, tmp_path):
    """Make it impossible for a test to reach hardware.

    Every test wires its service to fakes. Should one ever build a service with
    the real defaults, two things stop it: the environment names a port that does
    not exist, which also switches discovery off, and opening any real pyserial
    port raises. A developer's own TPS25751_BENCH_* settings are removed.
    """
    for name in ("TPS25751_BENCH_SERIAL", "TPS25751_BENCH_LOG"):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("TPS25751_BENCH_PORT", str(tmp_path / "no-such-port"))

    def refuse(self):
        raise serial.SerialException(f"a test tried to open the real serial port {self.port}")

    monkeypatch.setattr(serial.Serial, "open", refuse)
