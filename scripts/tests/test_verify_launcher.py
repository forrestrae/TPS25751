"""scripts/verify.py runs main's copy of verify_gate.py, never the branch's."""

from __future__ import annotations

import subprocess
import sys
import unittest

from tests.helpers import SCRIPTS, TempRepo

import workflow_common as wc

# A stand-in gate: prints its label and exits with its code. No tests, no pio.
GATE = "import sys\nprint('gate:{label}')\nsys.exit({code})\n"


class VerifyLauncherTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repo = TempRepo()
        self.addCleanup(self.repo.cleanup)
        (self.repo.path / "scripts").mkdir()
        support = (SCRIPTS / "workflow_common.py").read_text()
        self.repo.commit("chore: add workflow_common", support, "scripts/workflow_common.py")

    def gate(self, label: str, code: int, message: str) -> None:
        self.repo.commit(message, GATE.format(label=label, code=code), "scripts/verify_gate.py")

    def branch(self) -> None:
        self.repo.git("checkout", "-q", "-b", "feat/x")

    def verify(self) -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, str(SCRIPTS / "verify.py")], cwd=self.repo.path,
                              env=self.repo.env, capture_output=True, text=True)

    def verified(self) -> set[str]:
        return wc.verified_commits(self.repo.path)

    def test_branch_edit_to_gate_is_ignored(self):
        self.gate("main", 0, "chore: add passing gate")
        self.branch()
        self.gate("branch", 1, "chore: break the gate on the branch")
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("gate:main", result.stdout)
        self.assertNotIn("gate:branch", result.stdout)
        self.assertIn(self.repo.git("rev-parse", "HEAD"), self.verified())

    def test_branch_cannot_weaken_failing_gate(self):
        self.gate("main", 1, "chore: add failing gate")
        self.branch()
        self.gate("branch", 0, "chore: make the gate pass on the branch")
        result = self.verify()
        self.assertEqual(result.returncode, 1)
        self.assertIn("gate:main", result.stdout)
        self.assertEqual(self.verified(), set())

    def test_bootstrap_uses_branch_gate_when_main_has_none(self):
        self.branch()
        self.gate("branch", 0, "chore: introduce the gate")
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("bootstrap", result.stdout)
        self.assertIn("gate:branch", result.stdout)

    def test_running_gate_directly_never_stamps(self):
        self.gate("main", 0, "chore: add passing gate")
        self.branch()
        self.repo.commit("feat: something")
        subprocess.run([sys.executable, "scripts/verify_gate.py"], cwd=self.repo.path,
                       env=self.repo.env, capture_output=True, check=True)
        self.assertEqual(self.verified(), set())

    def test_dirty_tree_refused_before_gate_runs(self):
        self.gate("main", 0, "chore: add passing gate")
        self.branch()
        self.repo.commit("feat: something")
        (self.repo.path / "untracked").write_text("x\n")
        result = self.verify()
        self.assertEqual(result.returncode, 1)
        self.assertNotIn("gate:", result.stdout)


if __name__ == "__main__":
    unittest.main()
