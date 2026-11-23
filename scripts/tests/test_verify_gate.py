"""verify_gate.py runs the PD fixture check and the bench tests once their targets exist."""

from __future__ import annotations

import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from tests.helpers import SCRIPTS  # noqa: F401  (puts scripts/ on sys.path)

import verify_gate as vg

FIXTURE_CHECK = [sys.executable, "test/support/pd_capture_fixture.py", "--check"]
BENCH_TESTS = ["uv", "run", "--extra", "dev", "pytest"]


class FakeRun:
    """Records each command and its directory; returns the code set for a command's first word."""

    def __init__(self, codes: dict[str, int] | None = None, missing: str | None = None) -> None:
        self.calls: list[tuple[list[str], Path]] = []
        self.codes = codes or {}
        self.missing = missing

    def __call__(self, cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
        if cmd[0] == self.missing:
            raise FileNotFoundError(cmd[0])
        self.calls.append((cmd, cwd))
        return subprocess.CompletedProcess(cmd, self.codes.get(" ".join(cmd), 0))

    def names(self) -> list[list[str]]:
        return [cmd for cmd, _ in self.calls]


class VerifyGateStepTests(unittest.TestCase):
    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve()

    def add_fixture_script(self) -> None:
        (self.root / "test" / "support").mkdir(parents=True)
        (self.root / "test" / "support" / "pd_capture_fixture.py").write_text("")

    def add_bench(self) -> None:
        (self.root / "tools" / "pd-bench").mkdir(parents=True)

    def gate(self, run: FakeRun) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = vg.run_gates(self.root, run=run)
        return code, out.getvalue(), err.getvalue()

    def test_order_puts_the_new_steps_before_the_pio_steps(self):
        self.assertEqual([g.name for g in vg.GATES], [
            "script unit tests", "PD capture fixtures", "bench server tests",
            "native unit tests", "example build",
        ])

    def test_missing_targets_are_skipped_with_a_notice(self):
        run = FakeRun()
        code, out, _ = self.gate(run)
        self.assertEqual(code, 0)
        self.assertNotIn(FIXTURE_CHECK, run.names())
        self.assertNotIn(BENCH_TESTS, run.names())
        self.assertIn("verify: skipping PD capture fixtures: test/support/pd_capture_fixture.py "
                      "does not exist yet", out)
        self.assertIn("verify: skipping bench server tests: tools/pd-bench does not exist yet", out)
        self.assertEqual(len(run.calls), 3)  # script tests, native tests, example build

    def test_fixture_check_runs_from_the_root_once_the_script_exists(self):
        self.add_fixture_script()
        run = FakeRun()
        code, out, _ = self.gate(run)
        self.assertEqual(code, 0)
        self.assertIn((FIXTURE_CHECK, self.root), run.calls)
        self.assertIn("verify: running PD capture fixtures:", out)

    def test_bench_tests_run_in_tools_pd_bench_once_it_exists(self):
        self.add_bench()
        run = FakeRun()
        code, _, _ = self.gate(run)
        self.assertEqual(code, 0)
        self.assertIn((BENCH_TESTS, self.root / "tools" / "pd-bench"), run.calls)

    def test_a_stale_fixture_fails_the_gate_before_the_pio_steps(self):
        self.add_fixture_script()
        run = FakeRun(codes={" ".join(FIXTURE_CHECK): 1})
        code, _, err = self.gate(run)
        self.assertEqual(code, 1)
        self.assertIn("verify: FAILED: PD capture fixtures", err)
        self.assertNotIn(["pio", "test", "-e", "native"], run.names())

    def test_a_failing_bench_suite_fails_the_gate(self):
        self.add_bench()
        run = FakeRun(codes={" ".join(BENCH_TESTS): 1})
        code, _, err = self.gate(run)
        self.assertEqual(code, 1)
        self.assertIn("verify: FAILED: bench server tests", err)

    def test_missing_uv_fails_rather_than_skips(self):
        self.add_bench()
        code, _, err = self.gate(FakeRun(missing="uv"))
        self.assertEqual(code, 1)
        self.assertIn("bench server tests: uv is not installed or not on PATH", err)


if __name__ == "__main__":
    unittest.main()
