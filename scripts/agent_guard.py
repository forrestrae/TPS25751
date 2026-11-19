#!/usr/bin/env python3
"""Coding-agent hook that keeps agents inside the repository's git workflow.

Usage (configured in .claude/settings.json and .codex/hooks.json):
  agent_guard.py <claude|codex> pre-tool-use     PreToolUse hook; tool call JSON on stdin
  agent_guard.py <claude|codex> session-start    SessionStart hook; warns if hooks are off

The rules are shared. Each agent gets an adapter for its payload (Claude Code:
Bash, Write, Edit, MultiEdit, NotebookEdit; Codex: its shell tool, which may pass
an argv list, and apply_patch) and for its deny protocol (Claude Code: exit 2
with the reason on stderr; Codex: a permissionDecision "deny" JSON on stdout).

PreToolUse denies:
  - git push (agents never push; the human pushes at the end)
  - skipping git hooks: --no-verify, `git commit -n`
  - changing core.hooksPath, branch.main.mergeoptions or pull.ff, through
    `git config`, `git -c`, or GIT_CONFIG_* environment variables
  - writing to the workflow tooling (.githooks/, the scripts that enforce
    the workflow, .claude/settings*.json, the publish workflow) or to the
    verify stamp files, through Write/Edit or through Bash

The git hooks are the real gate; this hook closes the obvious ways an agent
could switch them off. A human who needs an agent to change the tooling
starts the session with WORKFLOW_GUARD_OFF=1 in the environment.
"""

from __future__ import annotations

import json
import os
import shlex
import sys
from pathlib import Path

PROTECTED_KEYS = ("core.hookspath", "branch.main.mergeoptions", "pull.ff")

PROTECTED_PATHS = (
    ".githooks/",
    "scripts/workflow_common.py",
    "scripts/gitguard.py",
    "scripts/check_commits.py",
    "scripts/release_level.py",
    "scripts/verify.py",
    "scripts/integrate.py",
    "scripts/setup_hooks.sh",
    "scripts/agent_guard.py",
    ".claude/settings.json",
    ".claude/settings.local.json",
    ".codex/",
    ".github/workflows/publish.yml",
)
STAMP_NAMES = ("verified-commits", "workflow-baseline")

# Programs that only read, so naming a protected path with them is harmless.
READ_ONLY_PROGRAMS = {
    "cat", "head", "tail", "less", "more", "grep", "egrep", "rg", "wc", "ls",
    "diff", "file", "stat", "bat", "nl", "cmp", "sha256sum", "shasum", "md5",
}
READ_ONLY_GIT = {"log", "show", "diff", "status", "blame", "ls-files", "grep", "rev-parse"}
WORKFLOW_ENTRYPOINTS = ("scripts/verify.py", "scripts/integrate.py", "scripts/check_commits.py",
                        "scripts/release_level.py")
SHELLS = {"sh", "bash", "zsh", "dash"}
SEPARATORS = {";", "&&", "||", "|", "&", "|&", "(", ")", "\n"}
REDIRECTS = {">", ">>", ">|", "&>", "<>"}

# git global options that take a separate value argument
GIT_OPTS_WITH_VALUE = {"-C", "-c", "--git-dir", "--work-tree", "--namespace", "--exec-path",
                       "--config-env", "--super-prefix"}


class Denied(Exception):
    pass


def _tokenize(command: str) -> list[str]:
    lexer = shlex.shlex(command.replace("\n", " ; "), posix=True, punctuation_chars=";&|()<>")
    lexer.whitespace_split = True
    lexer.commenters = ""
    try:
        return list(lexer)
    except ValueError:
        # Unbalanced quotes: fall back to a plain split so checks still run.
        return command.split()


def _segments(tokens: list[str]) -> list[list[str]]:
    segs, cur = [], []
    for tok in tokens:
        if tok in SEPARATORS:
            if cur:
                segs.append(cur)
            cur = []
        else:
            cur.append(tok)
    if cur:
        segs.append(cur)
    return segs


def _mentions_protected(text: str) -> bool:
    return any(p in text for p in PROTECTED_PATHS)


# `git commit` short options that consume a value (attached or as the next token)
COMMIT_VALUE_FLAGS = set("mFCcSt")


def _commit_has_short_n(args: list[str]) -> bool:
    """True if a short-option cluster passed to `git commit` contains -n."""
    skip_next = False
    for tok in args:
        if skip_next:
            skip_next = False
            continue
        if tok == "--":
            break
        if not tok.startswith("-") or tok.startswith("--") or len(tok) < 2:
            continue
        for pos, ch in enumerate(tok[1:]):
            if ch == "n":
                return True
            if ch in COMMIT_VALUE_FLAGS:
                skip_next = pos == len(tok) - 2  # value is the next token
                break
    return False


def _check_git(args: list[str]) -> None:
    i = 0
    while i < len(args) and args[i].startswith("-"):
        opt = args[i]
        if opt == "-c" and i + 1 < len(args):
            if args[i + 1].split("=", 1)[0].lower() in PROTECTED_KEYS:
                raise Denied(f"git -c {args[i + 1]} would change a workflow setting")
        if opt in GIT_OPTS_WITH_VALUE:
            i += 2
        else:
            i += 1
    if i >= len(args):
        return
    sub, rest = args[i], args[i + 1:]

    if sub == "push":
        raise Denied("agents never push; the human pushes main at the end of the workflow")
    if "--no-verify" in rest:
        raise Denied("--no-verify skips the repository's git hooks")
    if sub == "commit" and _commit_has_short_n(rest):
        raise Denied("`git commit -n` skips the repository's git hooks")
    if sub == "config":
        keys = [t for t in rest if t.lower() in PROTECTED_KEYS]
        read_only = any(t in ("--get", "--get-all", "--list", "-l", "--get-regexp", "get", "list")
                        for t in rest)
        if keys and not read_only:
            raise Denied(f"changing {keys[0]} would switch off part of the git workflow")
    if _mentions_protected(" ".join(rest)) and sub not in READ_ONLY_GIT:
        raise Denied(f"git {sub} on workflow tooling; the human maintains those files")


def check_bash(command: str, depth: int = 0) -> None:
    if any(name in command for name in STAMP_NAMES):
        raise Denied("the verify stamp files are written only by scripts/verify.py and setup")
    tokens = _tokenize(command)
    writes_redirect = any(t in REDIRECTS for t in tokens)
    for seg in _segments(tokens):
        argv = list(seg)
        while argv and "=" in argv[0] and not argv[0].startswith("-") and argv[0].split("=")[0].isidentifier():
            if argv[0].upper().startswith("GIT_CONFIG"):
                raise Denied("GIT_CONFIG_* variables can override the workflow's git settings")
            argv.pop(0)
        while argv and argv[0] in ("env", "command", "exec", "nohup", "time", "rtk", "sudo"):
            argv.pop(0)
            while argv and (argv[0].startswith("-") or ("=" in argv[0] and argv[0].split("=")[0].isidentifier())):
                if argv[0].upper().startswith("GIT_CONFIG"):
                    raise Denied("GIT_CONFIG_* variables can override the workflow's git settings")
                argv.pop(0)
        if not argv:
            continue
        prog = os.path.basename(argv[0])

        if prog == "git":
            _check_git(argv[1:])
            continue
        if prog in SHELLS and "-c" in argv and depth < 3:
            idx = argv.index("-c")
            if idx + 1 < len(argv):
                check_bash(argv[idx + 1], depth + 1)
            continue
        seg_text = " ".join(argv)
        if not _mentions_protected(seg_text):
            continue
        if prog.startswith("python") and len(argv) >= 2 and argv[1].endswith(WORKFLOW_ENTRYPOINTS):
            continue
        if any(argv[0].endswith(e) for e in WORKFLOW_ENTRYPOINTS):
            continue
        if prog in READ_ONLY_PROGRAMS and not writes_redirect:
            continue
        if prog == "sed" and "-i" not in argv and not any(a.startswith("-i") for a in argv) and not writes_redirect:
            continue
        raise Denied(f"`{prog}` touching workflow tooling; the human maintains those files")


def check_path(path: str, project_dir: str) -> None:
    p = Path(path)
    if any(p.name == name for name in STAMP_NAMES):
        raise Denied("the verify stamp files are written only by scripts/verify.py and setup")
    try:
        rel = p.resolve().relative_to(Path(project_dir).resolve()).as_posix()
    except ValueError:
        return
    for prot in PROTECTED_PATHS:
        if rel == prot or (prot.endswith("/") and rel.startswith(prot)):
            raise Denied(f"{rel} is workflow tooling; the human maintains it")


# --------------------------------------------------------------------------
# Agent adapters: turn each agent's PreToolUse payload into shell commands and
# file paths to check.
# --------------------------------------------------------------------------

SHELL_TOOLS = {"Bash", "shell", "local_shell", "exec_command", "container.exec", "unified_exec"}
FILE_TOOLS = {"Write", "Edit", "MultiEdit", "NotebookEdit"}
PATCH_TOOLS = {"apply_patch"}
PATCH_PATH_PREFIXES = ("*** Add File: ", "*** Update File: ", "*** Delete File: ", "*** Move to: ")
AGENTS = ("claude", "codex")


def _command_text(command) -> str:
    """Shell command as text; Codex may pass an argv list such as [zsh, -lc, script]."""
    if isinstance(command, list):
        argv = [str(a) for a in command]
        if len(argv) >= 3 and os.path.basename(argv[0]) in SHELLS and argv[1] in ("-c", "-lc"):
            return argv[2]
        return shlex.join(argv)
    return str(command or "")


def _patch_paths(patch: str) -> list[str]:
    paths = []
    for line in patch.splitlines():
        for prefix in PATCH_PATH_PREFIXES:
            if line.startswith(prefix):
                paths.append(line[len(prefix):].strip())
    return paths


def extract(payload: dict) -> tuple[list[str], list[str]]:
    """(shell commands, file paths) that a tool call would run or write."""
    tool = payload.get("tool_name", "")
    tin = payload.get("tool_input", {})
    if not isinstance(tin, dict):
        tin = {"input": tin}
    commands, paths = [], []
    if tool in SHELL_TOOLS:
        commands.append(_command_text(tin.get("command", tin.get("cmd", ""))))
    elif tool in FILE_TOOLS:
        paths.append(tin.get("file_path") or tin.get("notebook_path") or "")
    elif tool in PATCH_TOOLS:
        for key in ("input", "patch", "command"):
            value = tin.get(key)
            text = _command_text(value) if isinstance(value, list) else (value or "")
            paths.extend(_patch_paths(text))
    else:
        # Unknown tool: check any obvious command or path fields it carries.
        for key in ("command", "cmd"):
            if key in tin:
                commands.append(_command_text(tin[key]))
        for key in ("file_path", "path", "notebook_path"):
            if isinstance(tin.get(key), str):
                paths.append(tin[key])
    return commands, [p for p in paths if p]


def _project_dir(payload: dict, env: dict[str, str]) -> str:
    if env.get("CLAUDE_PROJECT_DIR"):
        return env["CLAUDE_PROJECT_DIR"]
    cwd = payload.get("cwd") or os.getcwd()
    import subprocess
    top = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=cwd,
                         capture_output=True, text=True).stdout.strip()
    return top or cwd


def _deny(agent: str, reason: str) -> int:
    message = (f"Blocked by the git workflow guard (scripts/agent_guard.py): {reason}. "
               f"See AGENTS.md, 'Git Workflow'.")
    if agent == "codex":
        # Codex reads the Claude-compatible JSON decision on stdout.
        print(json.dumps({"hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": message,
        }}))
        return 0
    print(message, file=sys.stderr)
    return 2


def pre_tool_use(payload: dict, env: dict[str, str], agent: str = "claude") -> int:
    if env.get("WORKFLOW_GUARD_OFF") == "1":
        return 0
    commands, paths = extract(payload)
    try:
        for command in commands:
            check_bash(command)
        if paths:
            project_dir = _project_dir(payload, env)
            for path in paths:
                full = path if os.path.isabs(path) else os.path.join(payload.get("cwd") or project_dir, path)
                check_path(full, project_dir)
    except Denied as exc:
        return _deny(agent, str(exc))
    return 0


def session_start(env: dict[str, str], cwd: str | None = None) -> int:
    import subprocess
    project_dir = env.get("CLAUDE_PROJECT_DIR") or cwd or os.getcwd()
    value = subprocess.run(["git", "config", "--get", "core.hooksPath"], cwd=project_dir,
                           capture_output=True, text=True).stdout.strip()
    if value != ".githooks":
        print("WARNING: this clone's git workflow hooks are NOT active (core.hooksPath is "
              f"{value or 'unset'}). Tell the user to run scripts/setup_hooks.sh before any "
              "commit; see AGENTS.md, 'Git Workflow'.")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] in AGENTS and argv[2] in ("pre-tool-use", "session-start"):
        agent, event = argv[1], argv[2]
        try:
            payload = json.load(sys.stdin) if event == "pre-tool-use" or not sys.stdin.isatty() else {}
        except json.JSONDecodeError:
            payload = {}
        if event == "pre-tool-use":
            return pre_tool_use(payload, dict(os.environ), agent)
        return session_start(dict(os.environ), payload.get("cwd") if isinstance(payload, dict) else None)
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
