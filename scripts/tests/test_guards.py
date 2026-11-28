import io
import json
import unittest
from contextlib import redirect_stderr, redirect_stdout

from tests.helpers import TempRepo
from tests.test_workflow_common import chdir_env
import agent_guard as cg
import gitguard
import workflow_common as wc

PROJECT = "/proj"


def quiet(fn, *args):
    with redirect_stderr(io.StringIO()), redirect_stdout(io.StringIO()):
        return fn(*args)


def codex(tool_name, tool_input, cwd=PROJECT):
    """Run the guard as Codex; return True if it denied the call."""
    out = io.StringIO()
    payload = {"tool_name": tool_name, "tool_input": tool_input, "cwd": cwd,
               "hook_event_name": "PreToolUse"}
    with redirect_stdout(out), redirect_stderr(io.StringIO()):
        rc = cg.pre_tool_use(payload, {"CLAUDE_PROJECT_DIR": PROJECT}, "codex")
    assert rc == 0, "Codex adapter reports denial on stdout, exit 0"
    text = out.getvalue().strip()
    if not text:
        return False
    decision = json.loads(text)["hookSpecificOutput"]
    assert decision["hookEventName"] == "PreToolUse"
    return decision["permissionDecision"] == "deny"


class AgentDetectionTests(unittest.TestCase):
    def test_markers(self):
        self.assertEqual(wc.detect_agent({"CLAUDECODE": "1"}), "Claude Code")
        for var in ("CODEX_THREAD_ID", "CODEX_SESSION_ID", "CODEX_TOOL_CALL_ID", "CODEX_SANDBOX"):
            self.assertEqual(wc.detect_agent({var: "x"}), "Codex", var)
        self.assertEqual(wc.detect_agent({"WORKFLOW_AGENT": "1"}), "coding agent")
        self.assertIsNone(wc.detect_agent({"HOME": "/Users/x", "CODEX_HOME": "/x"}))
        self.assertIsNone(wc.detect_agent({"CLAUDECODE": ""}))


class CodexAdapterTests(unittest.TestCase):
    def test_shell_string_and_argv(self):
        self.assertTrue(codex("shell", {"command": "git push origin main"}))
        self.assertTrue(codex("exec_command", {"cmd": "git commit --no-verify -m 'feat: x'"}))
        self.assertTrue(codex("shell", {"command": ["/opt/homebrew/bin/zsh", "-lc", "git push"]}))
        self.assertTrue(codex("Bash", {"command": "git config core.hooksPath /dev/null"}))
        self.assertFalse(codex("shell", {"command": ["zsh", "-lc", "git status"]}))
        self.assertFalse(codex("shell", {"command": ["git", "log", "-1"]}))

    def test_apply_patch_paths(self):
        bad = "*** Begin Patch\n*** Update File: scripts/gitguard.py\n@@\n-x\n+y\n*** End Patch"
        good = "*** Begin Patch\n*** Add File: src/new.cpp\n+int x;\n*** End Patch"
        moved = "*** Begin Patch\n*** Update File: src/a.cpp\n*** Move to: .githooks/pre-push\n*** End Patch"
        self.assertTrue(codex("apply_patch", {"input": bad}))
        self.assertTrue(codex("apply_patch", {"command": ["apply_patch", bad]}))
        self.assertTrue(codex("apply_patch", {"input": moved}))
        self.assertTrue(codex("apply_patch", {"input": "*** Add File: .codex/hooks.json\n+{}"}))
        self.assertFalse(codex("apply_patch", {"input": good}))

    def test_unknown_tool_fields_checked(self):
        self.assertTrue(codex("some_future_tool", {"cmd": "git push"}))
        self.assertTrue(codex("some_future_tool", {"path": f"{PROJECT}/scripts/verify.py"}))
        self.assertFalse(codex("some_future_tool", {"query": "git push"}))

    def test_guard_off(self):
        payload = {"tool_name": "shell", "tool_input": {"command": "git push"}}
        self.assertEqual(quiet(cg.pre_tool_use, payload, {"WORKFLOW_GUARD_OFF": "1"}, "codex"), 0)


def bash(command):
    return quiet(cg.pre_tool_use, {"tool_name": "Bash", "tool_input": {"command": command}},
                           {"CLAUDE_PROJECT_DIR": PROJECT})


def write(path, tool="Write"):
    return quiet(cg.pre_tool_use, {"tool_name": tool, "tool_input": {"file_path": path}},
                           {"CLAUDE_PROJECT_DIR": PROJECT})


class ClaudeGuardBashTests(unittest.TestCase):
    def denied(self, command):
        self.assertEqual(bash(command), 2, command)

    def allowed(self, command):
        self.assertEqual(bash(command), 0, command)

    def test_push_denied_in_any_spelling(self):
        for c in ("git push origin main", "git -C /x push", "rtk git push", "cd x && git push -q",
                  "GIT_TRACE=1 git push", "env FOO=1 git push", "bash -c 'git push origin main'"):
            self.denied(c)

    def test_hook_skips_denied(self):
        for c in ("git commit --no-verify -m 'feat: x'", "git commit -n -m 'feat: x'",
                  "git commit -anm 'feat: x'", "git merge --no-verify feat/x",
                  "git rebase --no-verify main"):
            self.denied(c)

    def test_commit_message_flags_allowed(self):
        for c in ("git commit -m 'feat: x'", "git commit -am 'fix: no-op'", "git commit -m -n",
                  "git commit -F msg.txt", "git commit --amend --no-edit"):
            self.allowed(c)

    def test_config_changes_denied(self):
        for c in ("git config core.hooksPath /dev/null", "git config --unset core.hooksPath",
                  "git config branch.main.mergeoptions ''", "git -c core.hooksPath=/dev/null commit -m x",
                  "GIT_CONFIG_COUNT=1 git commit -m x", "git config pull.ff false"):
            self.denied(c)

    def test_config_reads_allowed(self):
        self.allowed("git config --get core.hooksPath")
        self.allowed("git config user.name")

    def test_stamp_files_denied(self):
        self.denied("echo abc >> .git/verified-commits")
        self.denied("cat .git/workflow-baseline")

    def test_tooling_writes_denied(self):
        for c in ("sed -i '' s/x/y/ scripts/gitguard.py", "cp /tmp/x .githooks/pre-push",
                  "echo x > scripts/verify.py", "rm .githooks/commit-msg",
                  "git checkout HEAD~1 -- scripts/workflow_common.py",
                  "python3 -c 'open(\"scripts/gitguard.py\",\"w\")'"):
            self.denied(c)

    def test_tooling_reads_and_entrypoints_allowed(self):
        for c in ("cat scripts/gitguard.py", "grep -n x .githooks/pre-push", "sed -n 1,5p scripts/verify.py",
                  "git diff main -- scripts/verify.py", "python3 scripts/verify.py",
                  "scripts/integrate.py feat/x", "python3 scripts/release_level.py v1..HEAD"):
            self.allowed(c)

    def test_tooling_reads_with_other_redirects_allowed(self):
        # A redirect anywhere in the command used to count as a write to the tooling.
        for c in ("grep -n native scripts/verify.py 2>/dev/null",
                  "ls 2>/dev/null; grep -n native scripts/verify.py",
                  "grep -n native scripts/verify.py > /tmp/out.txt",
                  "sed -n 1,5p scripts/verify.py 2>&1 | head",
                  "git log -- scripts/verify.py > /tmp/log.txt"):
            self.allowed(c)

    def test_redirects_into_tooling_denied(self):
        for c in ("cat a.txt >> scripts/verify.py", "sed s/a/b/ x > scripts/agent_guard.py",
                  "grep x y 2> .githooks/pre-push", "ls &> scripts/gitguard.py",
                  "git show HEAD~1:scripts/verify.py > scripts/verify.py",
                  "python3 scripts/verify.py > scripts/integrate.py"):
            self.denied(c)

    def test_gate_checks_editable(self):
        for c in ("echo x >> scripts/verify_gate.py", "sed -i '' s/x/y/ scripts/check_commits.py",
                  "echo x >> scripts/release_level.py"):
            self.allowed(c)
        self.denied("echo x >> scripts/verify.py")

    def test_ordinary_commands_allowed(self):
        for c in ("pio test -e native", "git status", "git merge --no-ff feat/x -m \"Merge branch 'feat/x'\"",
                  "git log --oneline | head", "ls -la"):
            self.allowed(c)

    def test_guard_off_switch(self):
        payload = {"tool_name": "Bash", "tool_input": {"command": "git push"}}
        self.assertEqual(quiet(cg.pre_tool_use, payload, {"WORKFLOW_GUARD_OFF": "1"}), 0)


class ClaudeGuardPathTests(unittest.TestCase):
    def test_protected_paths(self):
        for rel in (".githooks/pre-push", "scripts/gitguard.py", ".claude/settings.json",
                    ".claude/settings.local.json", ".github/workflows/publish.yml",
                    ".codex/hooks.json", "scripts/agent_guard.py"):
            self.assertEqual(write(f"{PROJECT}/{rel}"), 2, rel)
        self.assertEqual(write(f"{PROJECT}/.git/verified-commits", "Edit"), 2)

    def test_ordinary_paths(self):
        for rel in ("src/TPS25751.cpp", "AGENTS.md", "scripts/bump_version.py", "scripts/tests/test_x.py"):
            self.assertEqual(write(f"{PROJECT}/{rel}"), 0, rel)


class GitGuardTests(unittest.TestCase):
    def test_pre_push_refused_for_agents(self):
        self.assertEqual(quiet(gitguard.pre_push, [], {"CLAUDECODE": "1"}), 1)
        self.assertEqual(quiet(gitguard.pre_push, [], {"CODEX_THREAD_ID": "t"}), 1)
        self.assertEqual(quiet(gitguard.pre_push, [], {"WORKFLOW_AGENT": "1"}), 1)

    def test_reference_transaction_ignores_other_states_and_refs(self):
        self.assertEqual(quiet(gitguard.reference_transaction, "committed", ["a b refs/heads/main"]), 0)
        self.assertEqual(quiet(gitguard.reference_transaction, 
            "prepared", [f"{wc.ZERO_OID} {'1' * 40} refs/heads/feat/x"]), 0)
        self.assertEqual(quiet(gitguard.reference_transaction, 
            "prepared", ["ref:refs/heads/main ref:refs/heads/feat/x HEAD"]), 0)

    def test_reference_transaction_and_pre_push_on_repo(self):
        repo = TempRepo()
        try:
            main0 = repo.git("rev-parse", "HEAD")
            repo.git("switch", "-q", "-c", "feat/x")
            tip = repo.commit("feat: x", "1\n")
            repo.git("switch", "-q", "main")
            merge = repo.merge_commit(main0, tip)
            with chdir_env(repo):
                line = f"{main0} {merge} refs/heads/main"
                self.assertEqual(quiet(gitguard.reference_transaction, "prepared", [line]), 1)
                self.assertEqual(quiet(gitguard.pre_push, [f"refs/heads/main {merge} refs/heads/main {main0}"], {}), 1)
                wc.record_verified(tip)
                self.assertEqual(quiet(gitguard.reference_transaction, "prepared", [line]), 0)
                self.assertEqual(quiet(gitguard.pre_push, [f"refs/heads/main {merge} refs/heads/main {main0}"], {}), 0)
                self.assertEqual(quiet(gitguard.pre_push, [f"(delete) {wc.ZERO_OID} refs/heads/main {main0}"], {}), 1)
        finally:
            repo.cleanup()


if __name__ == "__main__":
    unittest.main()
