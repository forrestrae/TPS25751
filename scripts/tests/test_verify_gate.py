"""verify_gate.py runs the PD fixture checks and the bench tests once their targets exist."""

from __future__ import annotations

import io
import subprocess
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from tests.helpers import TempRepo

import verify_gate as vg

FIXTURE_CHECK = [sys.executable, "test/support/pd_capture_fixture.py", "--check"]
FIXTURE_TESTS = [sys.executable, "-m", "unittest", "discover", "-s", "test/support", "-p", "test_*.py", "-q"]
BENCH_TESTS = ["uv", "run", "--locked", "pytest"]


class FakeRun:
    """Records each command and its directory; returns the code set for a command, or raises."""

    def __init__(self, codes: dict[str, int] | None = None, raises: dict[str, OSError] | None = None,
                 effect=None) -> None:
        self.calls: list[tuple[list[str], Path]] = []
        self.codes = codes or {}
        self.raises = raises or {}
        self.effect = effect  # called with (cmd, cwd) before returning, to touch the tree

    def __call__(self, cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
        if cmd[0] in self.raises:
            raise self.raises[cmd[0]]
        self.calls.append((cmd, cwd))
        if self.effect:
            self.effect(cmd, cwd)
        return subprocess.CompletedProcess(cmd, self.codes.get(" ".join(cmd), 0))

    def names(self) -> list[list[str]]:
        return [cmd for cmd, _ in self.calls]


class VerifyGateStepTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repo = TempRepo()
        self.addCleanup(self.repo.cleanup)
        self.root = self.repo.path

    def add_fixture_script(self) -> None:
        (self.root / "test" / "support").mkdir(parents=True, exist_ok=True)
        (self.root / "test" / "support" / "pd_capture_fixture.py").write_text("")

    def add_fixture_tests(self) -> None:
        (self.root / "test" / "support").mkdir(parents=True, exist_ok=True)
        (self.root / "test" / "support" / "test_pd_capture_fixture.py").write_text("")

    def add_bench(self) -> None:
        (self.root / "tools" / "pd-bench").mkdir(parents=True)

    def gate(self, run: FakeRun, gates: list[vg.Gate] | None = None) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = vg.run_gates(self.root, gates if gates is not None else vg.GATES, run=run)
        return code, out.getvalue(), err.getvalue()

    def test_order_puts_the_new_steps_before_the_pio_steps(self):
        self.assertEqual([g.name for g in vg.GATES], [
            "script unit tests", "PD capture fixtures", "PD capture fixture tests",
            "bench server tests", "native unit tests", "example build",
        ])

    def test_bench_tests_use_the_lock_file_as_is(self):
        bench = next(g for g in vg.GATES if g.name == "bench server tests")
        self.assertEqual(bench.cmd, BENCH_TESTS)
        self.assertEqual(bench.cwd, "tools/pd-bench")

    def test_missing_targets_are_skipped_with_a_notice(self):
        run = FakeRun()
        code, out, _ = self.gate(run)
        self.assertEqual(code, 0)
        for cmd in (FIXTURE_CHECK, FIXTURE_TESTS, BENCH_TESTS):
            self.assertNotIn(cmd, run.names())
        self.assertIn("verify: skipping PD capture fixtures: test/support/pd_capture_fixture.py "
                      "does not exist yet", out)
        self.assertIn("verify: skipping PD capture fixture tests: "
                      "test/support/test_pd_capture_fixture.py does not exist yet", out)
        self.assertIn("verify: skipping bench server tests: tools/pd-bench does not exist yet", out)
        self.assertEqual(len(run.calls), 3)  # script tests, native tests, example build

    def test_fixture_check_and_tests_run_from_the_root_once_they_exist(self):
        self.add_fixture_script()
        self.add_fixture_tests()
        run = FakeRun()
        code, out, _ = self.gate(run)
        self.assertEqual(code, 0)
        self.assertIn((FIXTURE_CHECK, self.root), run.calls)
        self.assertIn((FIXTURE_TESTS, self.root), run.calls)
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
        code, _, err = self.gate(FakeRun(raises={"uv": FileNotFoundError(2, "No such file", "uv")}))
        self.assertEqual(code, 1)
        self.assertIn("bench server tests: uv is not installed or not on PATH", err)

    def test_missing_working_directory_is_named_as_such(self):
        # The step's target exists but its working directory does not.
        self.add_fixture_script()
        gates = [vg.Gate("odd step", ["true"], cwd="tools/absent", requires="test/support/pd_capture_fixture.py")]
        run = FakeRun()
        code, _, err = self.gate(run, gates)
        self.assertEqual(code, 1)
        self.assertIn("verify: FAILED: odd step: working directory tools/absent does not exist", err)
        self.assertNotIn("not installed", err)
        self.assertEqual(run.calls, [])

    def test_other_os_errors_are_reported_not_raised(self):
        self.add_bench()
        code, _, err = self.gate(FakeRun(raises={"uv": PermissionError(13, "Permission denied", "uv")}))
        self.assertEqual(code, 1)
        self.assertIn("verify: FAILED: bench server tests: cannot run uv: Permission denied", err)

    def test_untracked_venv_left_by_the_bench_tests_fails_the_gate(self):
        self.add_bench()

        def make_venv(cmd: list[str], cwd: Path) -> None:
            if cmd == BENCH_TESTS:
                (cwd / ".venv").mkdir()
                (cwd / ".venv" / "pyvenv.cfg").write_text("home = /usr\n")

        code, _, err = self.gate(FakeRun(effect=make_venv))
        self.assertEqual(code, 1)
        self.assertIn("bench server tests left untracked files; add them to .gitignore", err)
        self.assertIn("tools/pd-bench/.venv/pyvenv.cfg", err)

    def test_ignored_venv_passes(self):
        self.add_bench()
        (self.root / ".gitignore").write_text("tools/pd-bench/.venv/\n")

        def make_venv(cmd: list[str], cwd: Path) -> None:
            if cmd == BENCH_TESTS:
                (cwd / ".venv").mkdir()
                (cwd / ".venv" / "pyvenv.cfg").write_text("home = /usr\n")

        code, _, err = self.gate(FakeRun(effect=make_venv))
        self.assertEqual(code, 0, err)

    def test_repository_gitignore_covers_the_bench_venv_and_captures(self):
        root = Path(__file__).resolve().parents[2]
        for path in ("tools/pd-bench/.venv/pyvenv.cfg", "captures/x.jsonl"):
            ignored = subprocess.run(["git", "check-ignore", "-q", path], cwd=root).returncode == 0
            self.assertTrue(ignored, path)


if __name__ == "__main__":
    unittest.main()
