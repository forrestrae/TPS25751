import os
import unittest
from contextlib import contextmanager

from tests.helpers import TempRepo
import workflow_common as wc


@contextmanager
def chdir_env(repo: TempRepo):
    """Run workflow_common's git calls against `repo`."""
    old_cwd, old_env = os.getcwd(), dict(os.environ)
    os.chdir(repo.path)
    os.environ.clear()
    os.environ.update(repo.env)
    try:
        yield
    finally:
        os.chdir(old_cwd)
        os.environ.clear()
        os.environ.update(old_env)


class MessageTests(unittest.TestCase):
    def ok(self, msg, **kw):
        self.assertIsNone(wc.check_message(msg, **kw), msg)

    def bad(self, msg, **kw):
        self.assertIsNotNone(wc.check_message(msg, **kw), msg)

    def test_conventional_subjects(self):
        self.ok("feat: add thing")
        self.ok("fix(rev-b): decode GPIO 10 and 11")
        self.ok("feat(api)!: drop old call")
        self.ok("chore(release): v2.0.2 [skip ci]")
        self.ok("docs(batch-f): note\n\nBody text.")

    def test_rejected_subjects(self):
        self.bad("update: revise plan")
        self.bad("add: new register")
        self.bad("Feat: capitalised type")
        self.bad("feat:missing space")
        self.bad("feat(): empty scope")
        self.bad("")
        self.bad("feat: " + "x" * 100)

    def test_merge_subjects(self):
        self.ok("Merge branch 'feat/registers-batch-c'")
        self.bad("Merge feature branch #minor")
        self.bad("Merge branch 'Some_Branch'")

    def test_revert_and_autosquash(self):
        self.ok('Revert "feat: add thing"')
        self.bad('Revert "update: x"')
        self.bad("fixup! feat: add thing")
        self.ok("fixup! feat: add thing", allow_autosquash=True)

    def test_attribution_rejected(self):
        self.bad("feat: x\n\nCo-Authored-By: Claude <noreply@anthropic.com>")
        self.ok("feat: x\n\nCo-Authored-By: Pat <pat@example.invalid>")

    def test_clean_message_strips_comments_and_scissors(self):
        raw = "feat: x\n# comment\n\nbody\n# ------------------------ >8 ------------------------\ndiff"
        self.assertEqual(wc.clean_message(raw), "feat: x\n\nbody")


class BranchTests(unittest.TestCase):
    def test_valid(self):
        for name in ("feat/registers-batch-c", "fix/rev-b-trm", "ci/git-workflow", "docs/v2.1-notes"):
            self.assertIsNone(wc.check_branch_name(name), name)

    def test_invalid(self):
        for name in ("main", "feature/x", "feat/X", "feat_x", "feat/", "feat/a--b", "fix/rev_b"):
            self.assertIsNotNone(wc.check_branch_name(name), name)


class ReleaseLevelTests(unittest.TestCase):
    def test_levels(self):
        self.assertEqual(wc.release_level([]), "patch")
        self.assertEqual(wc.release_level(["fix: a", "docs: b"]), "patch")
        self.assertEqual(wc.release_level(["fix: a", "feat(x): b"]), "minor")
        self.assertEqual(wc.release_level(["feat: a", "fix!: b"]), "major")
        self.assertEqual(wc.release_level(["fix: a\n\nBREAKING CHANGE: gone"]), "major")

    def test_ignored_commits(self):
        self.assertEqual(wc.release_level(["Merge branch 'feat/x'", "chore(release): v1 [skip ci]"]), "patch")

    def test_tokens_have_no_effect(self):
        self.assertEqual(wc.release_level(["fix: a #minor", "docs: b #major"]), "patch")


class MainUpdateDecisionTests(unittest.TestCase):
    def setUp(self):
        self.repo = TempRepo()
        self.main0 = self.repo.git("rev-parse", "HEAD")
        self.repo.git("switch", "-q", "-c", "feat/x")
        self.tip = self.repo.commit("feat: x", "1\n")
        self.repo.git("switch", "-q", "main")

    def tearDown(self):
        self.repo.cleanup()

    def decide(self, old, new, verified=()):
        with chdir_env(self.repo):
            return wc.main_update_decision(old, new, verified=set(verified))

    def test_verified_merge_allowed(self):
        merge = self.repo.merge_commit(self.main0, self.tip)
        self.assertTrue(self.decide(self.main0, merge, {self.tip}).allowed)

    def test_unverified_merge_refused(self):
        merge = self.repo.merge_commit(self.main0, self.tip)
        self.assertFalse(self.decide(self.main0, merge).allowed)

    def test_fast_forward_refused(self):
        self.assertFalse(self.decide(self.main0, self.tip, {self.tip}).allowed)

    def test_direct_commit_refused(self):
        direct = self.repo.commit("feat: direct", "2\n")
        self.assertFalse(self.decide(self.main0, direct).allowed)

    def test_wrong_first_parent_refused(self):
        other = self.repo.commit("fix: other", "3\n", name="g")
        merge = self.repo.merge_commit(self.main0, self.tip)
        self.assertFalse(self.decide(other, merge, {self.tip}).allowed)

    def test_tree_mismatch_refused(self):
        moved = self.repo.commit("chore(release): v1 [skip ci]", "9\n", name="g")
        tree = self.repo.git("write-tree")  # main's tree plus g, without the branch change
        merge = self.repo.git("commit-tree", tree, "-p", moved, "-p", self.tip, "-m", "Merge branch 'feat/x'")
        decision = self.decide(moved, merge, {self.tip})
        self.assertFalse(decision.allowed)
        self.assertIn("differs", decision.reason)

    def test_origin_reachable_allowed(self):
        self.repo.git("update-ref", "refs/remotes/origin/main", self.tip)
        self.assertTrue(self.decide(self.main0, self.tip).allowed)

    def test_baseline_allowed(self):
        with chdir_env(self.repo):
            wc.baseline_file().write_text(self.tip + "\n")
        self.assertTrue(self.decide(self.main0, self.tip).allowed)

    def test_delete_refused_and_noop_allowed(self):
        self.assertFalse(self.decide(self.main0, wc.ZERO_OID).allowed)
        self.assertTrue(self.decide(self.main0, self.main0).allowed)

    def test_verified_file_roundtrip(self):
        with chdir_env(self.repo):
            wc.record_verified(self.tip)
            self.assertIn(self.tip, wc.verified_commits())


class HooksStatusTests(unittest.TestCase):
    """core.hooksPath counts as active when it names the repo's .githooks directory."""

    def setUp(self):
        self.repo = TempRepo()
        self.hooks = self.repo.path / ".githooks"
        self.hooks.mkdir()

    def tearDown(self):
        self.repo.cleanup()

    def status_for(self, value):
        if value is None:
            git_unchecked(self.repo, "config", "--unset-all", "core.hooksPath")
        else:
            self.repo.git("config", "core.hooksPath", value)
        with chdir_env(self.repo):
            return wc.hooks_status()

    def assertActive(self, value):
        status = self.status_for(value)
        self.assertTrue(status.active, f"{value!r} resolved to {status.resolved}")

    def assertInactive(self, value):
        self.assertFalse(self.status_for(value).active, repr(value))

    def test_unset_is_inactive(self):
        status = self.status_for(None)
        self.assertFalse(status.active)
        self.assertIsNone(status.configured)

    def test_relative_spellings(self):
        self.assertActive(".githooks")
        self.assertActive("./.githooks")
        self.assertActive(".githooks/")

    def test_absolute_path(self):
        self.assertActive(str(self.hooks))
        self.assertActive(str(self.hooks) + "/")

    def test_path_through_symlink(self):
        link = self.repo.path.parent / "hooks-link"
        link.symlink_to(self.hooks, target_is_directory=True)
        self.assertActive(str(link))
        repo_link = self.repo.path.parent / "repo-link"
        repo_link.symlink_to(self.repo.path, target_is_directory=True)
        self.assertActive(str(repo_link / ".githooks"))

    def test_home_relative(self):
        home = os.path.expanduser("~")
        try:
            rel = os.path.relpath(self.hooks, home)
        except ValueError:
            self.skipTest("hooks directory not reachable from the home directory")
        self.assertActive("~/" + rel)

    def test_other_directory_is_inactive(self):
        other = self.repo.path.parent / "elsewhere"
        other.mkdir()
        self.assertInactive(str(other))
        self.assertInactive("hooks")
        self.assertInactive("/dev/null")

    def test_session_start_warns_only_when_inactive(self):
        import io
        from contextlib import redirect_stdout
        import agent_guard

        def output(value):
            self.status_for(value)
            out = io.StringIO()
            with chdir_env(self.repo), redirect_stdout(out):
                agent_guard.session_start({"CLAUDE_PROJECT_DIR": str(self.repo.path)})
            return out.getvalue()

        self.assertEqual(output(str(self.hooks)), "")
        self.assertEqual(output(".githooks"), "")
        warning = output(str(self.repo.path.parent))
        self.assertIn("NOT active", warning)
        self.assertIn("resolves to", warning)
        self.assertIn("unset", output(None))


def git_unchecked(repo, *args):
    """Run git in `repo`, ignoring the exit status (unsetting an unset key exits 5)."""
    import subprocess
    subprocess.run(["git", *args], cwd=repo.path, env=repo.env, capture_output=True)


if __name__ == "__main__":
    unittest.main()
