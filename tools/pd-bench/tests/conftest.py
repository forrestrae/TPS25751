import pytest

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
def no_bench_environment(monkeypatch):
    """Keep a developer's own TPS25751_BENCH_* settings out of the tests."""
    for name in ("TPS25751_BENCH_PORT", "TPS25751_BENCH_SERIAL", "TPS25751_BENCH_LOG"):
        monkeypatch.delenv(name, raising=False)
