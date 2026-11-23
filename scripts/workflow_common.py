"""Shared rules and git helpers for the repository's git workflow.

The workflow itself is described in AGENTS.md under "Git Workflow". Every
enforcement point (git hooks, scripts/verify.py, scripts/integrate.py, CI,
and the Claude Code guard) imports its rules from here so that they cannot
drift apart.

Standard library only: this runs from git hooks and in CI before any
dependencies are installed.
"""

from __future__ import annotations

import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

# --------------------------------------------------------------------------
# Naming rules
# --------------------------------------------------------------------------

COMMIT_TYPES = ("feat", "fix", "docs", "refactor", "test", "chore", "perf", "build", "ci")
_TYPES = "|".join(COMMIT_TYPES)

MAIN_BRANCH = "main"
MAIN_REF = "refs/heads/main"
ORIGIN_MAIN_REF = "refs/remotes/origin/main"
ARCHIVE_TAG_PREFIX = "archive/"
MAX_SUBJECT_LEN = 100

BRANCH_RE = re.compile(rf"^(?:{_TYPES})/[a-z0-9]+(?:[.-][a-z0-9]+)*$")
CONVENTIONAL_RE = re.compile(rf"^(?P<type>{_TYPES})(?:\((?P<scope>[a-z0-9][a-z0-9._/-]*)\))?(?P<bang>!)?: \S")
MERGE_RE = re.compile(r"^Merge branch '(?P<branch>[^']+)'$")
REVERT_RE = re.compile(r'^Revert "(?P<inner>.+)"$')
AUTOSQUASH_RE = re.compile(r"^(?:fixup|squash|amend)! ")
RELEASE_RE = re.compile(r"^chore\(release\): ")
BREAKING_FOOTER_RE = re.compile(r"^BREAKING[ -]CHANGE: ", re.MULTILINE)
ATTRIBUTION_RE = re.compile(
    r"^(?:Co-Authored-By:.*\b(?:Claude|Anthropic)\b|.*Generated with \[?Claude)", re.IGNORECASE | re.MULTILINE
)

ZERO_OID = "0" * 40

# Environment variables that coding agents set in the shells they run commands in.
# Claude Code sets CLAUDECODE=1. Codex sets CODEX_THREAD_ID, CODEX_SESSION_ID and
# CODEX_TOOL_CALL_ID on every command (CODEX_SANDBOX only when sandboxed).
# WORKFLOW_AGENT=1 lets any other agent opt in.
AGENT_MARKERS = (
    ("Claude Code", ("CLAUDECODE",)),
    ("Codex", ("CODEX_THREAD_ID", "CODEX_SESSION_ID", "CODEX_TOOL_CALL_ID", "CODEX_SANDBOX")),
    ("coding agent", ("WORKFLOW_AGENT",)),
)


def detect_agent(env: dict[str, str]) -> Optional[str]:
    """Name of the coding agent whose shell this is, or None for a human shell."""
    for name, variables in AGENT_MARKERS:
        if any(env.get(var) for var in variables):
            return name
    return None


def check_branch_name(name: str) -> Optional[str]:
    """Return an error string if `name` is not a valid topic-branch name."""
    if name == MAIN_BRANCH:
        return "work happens on a topic branch, not on main"
    if not BRANCH_RE.match(name):
        return (
            f"branch {name!r} must be <type>/<kebab-topic> with type one of "
            f"{', '.join(COMMIT_TYPES)} (lowercase letters, digits, '-' and '.')"
        )
    return None


def clean_message(raw: str, comment_char: str = "#") -> str:
    """Strip git comment lines and everything below a scissors line."""
    lines = []
    for line in raw.splitlines():
        if line.startswith(f"{comment_char} ------------------------ >8 ------------------------"):
            break
        if line.startswith(comment_char):
            continue
        lines.append(line.rstrip())
    return "\n".join(lines).strip("\n")


def check_message(message: str, allow_autosquash: bool = False) -> Optional[str]:
    """Return an error string if a commit message breaks the convention."""
    message = message.strip("\n")
    if not message.strip():
        return "empty commit message"
    subject = message.splitlines()[0]

    if allow_autosquash and AUTOSQUASH_RE.match(subject):
        return None

    revert = REVERT_RE.match(subject)
    merge = MERGE_RE.match(subject)
    if merge:
        err = check_branch_name(merge.group("branch"))
        if err:
            return f"merge subject names an invalid branch: {err}"
    elif revert:
        if not CONVENTIONAL_RE.match(revert.group("inner")):
            return f"revert subject must wrap a conventional subject: {subject!r}"
    elif not CONVENTIONAL_RE.match(subject):
        return (
            f"subject {subject!r} is not 'type(scope): subject' with type one of "
            f"{', '.join(COMMIT_TYPES)}, or \"Merge branch '<branch>'\""
        )
    if len(subject) > MAX_SUBJECT_LEN and not revert:
        return f"subject is {len(subject)} characters; the limit is {MAX_SUBJECT_LEN}"
    if ATTRIBUTION_RE.search(message):
        return "attribution trailers are not used in this repository"
    return None


# --------------------------------------------------------------------------
# Release level
# --------------------------------------------------------------------------

LEVELS = ("patch", "minor", "major")


def commit_level(message: str) -> Optional[str]:
    """Release level implied by one commit, or None if it does not count."""
    subject = message.strip("\n").splitlines()[0] if message.strip() else ""
    if not subject or MERGE_RE.match(subject) or RELEASE_RE.match(subject):
        return None
    match = CONVENTIONAL_RE.match(subject)
    if (match and match.group("bang")) or BREAKING_FOOTER_RE.search(message):
        return "major"
    if match and match.group("type") == "feat":
        return "minor"
    return "patch"


def release_level(messages: Iterable[str]) -> str:
    """Highest release level implied by `messages` (patch when none count)."""
    level = 0
    for message in messages:
        found = commit_level(message)
        if found:
            level = max(level, LEVELS.index(found))
    return LEVELS[level]


# --------------------------------------------------------------------------
# Git helpers
# --------------------------------------------------------------------------


class GitError(RuntimeError):
    pass


def git(*args: str, cwd: Optional[Path] = None, check: bool = True) -> str:
    proc = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise GitError(f"git {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout.strip()


def git_ok(*args: str, cwd: Optional[Path] = None) -> bool:
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True).returncode == 0


def repo_root(cwd: Optional[Path] = None) -> Path:
    return Path(git("rev-parse", "--show-toplevel", cwd=cwd))


def verified_file(cwd: Optional[Path] = None) -> Path:
    """Stamp file shared by all worktrees of this clone."""
    common = Path(git("rev-parse", "--git-common-dir", cwd=cwd))
    if not common.is_absolute():
        common = (Path(cwd) if cwd else Path.cwd()) / common
    return common.resolve() / "verified-commits"


def verified_commits(cwd: Optional[Path] = None) -> set[str]:
    path = verified_file(cwd)
    if not path.exists():
        return set()
    return {line.split()[0] for line in path.read_text().splitlines() if line.strip()}


def record_verified(sha: str, cwd: Optional[Path] = None) -> None:
    with verified_file(cwd).open("a") as fh:
        fh.write(f"{sha}\n")


def baseline_file(cwd: Optional[Path] = None) -> Path:
    """Main's tip when the hooks were installed (see scripts/setup_hooks.sh).

    History up to this commit predates the workflow and is accepted as is.
    """
    return verified_file(cwd).with_name("workflow-baseline")


def baseline(cwd: Optional[Path] = None) -> Optional[str]:
    path = baseline_file(cwd)
    if not path.exists():
        return None
    text = path.read_text().strip()
    return text or None


def commit_messages(rev_range: str, cwd: Optional[Path] = None) -> list[tuple[str, str]]:
    """(sha, full message) for every commit in `rev_range`, oldest first."""
    out = subprocess.run(
        ["git", "log", "--reverse", "--format=%H%x00%B%x1e", rev_range],
        cwd=cwd, capture_output=True, text=True, check=True,
    ).stdout
    result = []
    for record in out.split("\x1e"):
        record = record.strip("\n")
        if record:
            sha, _, body = record.partition("\x00")
            result.append((sha.strip(), body))
    return result


def parents(sha: str, cwd: Optional[Path] = None) -> list[str]:
    return git("rev-list", "--parents", "-n", "1", sha, cwd=cwd).split()[1:]


def tree_of(sha: str, cwd: Optional[Path] = None) -> str:
    return git("rev-parse", f"{sha}^{{tree}}", cwd=cwd)


def is_ancestor(a: str, b: str, cwd: Optional[Path] = None) -> bool:
    return git_ok("merge-base", "--is-ancestor", a, b, cwd=cwd)


def ref_exists(ref: str, cwd: Optional[Path] = None) -> bool:
    return git_ok("rev-parse", "--verify", "--quiet", ref, cwd=cwd)


# --------------------------------------------------------------------------
# Are the workflow hooks active?
# --------------------------------------------------------------------------

HOOKS_DIR = ".githooks"


@dataclass
class HooksStatus:
    active: bool
    configured: Optional[str]   # core.hooksPath as configured, None when unset
    resolved: Optional[Path]    # where git looks for hooks with that value
    expected: Path              # the repository's own .githooks directory


def hooks_status(cwd: Optional[Path] = None) -> HooksStatus:
    """Whether core.hooksPath points git at this repository's .githooks directory.

    Compares paths, not strings: ".githooks", "./.githooks", an absolute path,
    a trailing slash, "~" or a path through a symlink all count as active when
    they name the same directory. As git does, a relative value is taken
    relative to the top of the working tree.
    """
    top = repo_root(cwd)
    expected = Path(os.path.realpath(top / HOOKS_DIR))
    configured = git("config", "--get", "core.hooksPath", cwd=cwd, check=False) or None
    if configured is None:
        return HooksStatus(False, None, None, expected)
    path = Path(os.path.expanduser(configured))
    if not path.is_absolute():
        path = top / path
    resolved = Path(os.path.realpath(path))
    return HooksStatus(resolved == expected, configured, resolved, expected)

# --------------------------------------------------------------------------
# The main-branch rule
# --------------------------------------------------------------------------


@dataclass
class Decision:
    allowed: bool
    reason: str


def main_update_decision(old: str, new: str, cwd: Optional[Path] = None,
                         verified: Optional[set[str]] = None) -> Decision:
    """Decide whether local main may move from `old` to `new`.

    Allowed: no-op updates; moving to a commit already on origin/main (for
    example pulling the release bot's commit) or within the history recorded
    as the baseline at hook installation; and a two-parent merge whose
    first parent is the current main, whose second parent passed
    scripts/verify.py, and whose tree equals that parent's tree (so the
    merged content is exactly what was tested).
    """
    if new == old:
        return Decision(True, "no change")
    if new == ZERO_OID:
        return Decision(False, "deleting main is not allowed")
    if ref_exists(ORIGIN_MAIN_REF, cwd) and is_ancestor(new, ORIGIN_MAIN_REF, cwd):
        return Decision(True, "commit is already on origin/main")
    base = baseline(cwd)
    if base and is_ancestor(new, base, cwd):
        return Decision(True, "commit predates the workflow baseline")

    verified = verified_commits(cwd) if verified is None else verified
    ps = parents(new, cwd)
    if len(ps) != 2:
        return Decision(False, (
            "main only moves by a --no-ff merge of a verified branch "
            "(direct commits, amends, resets and fast-forwards are refused); "
            "use scripts/integrate.py <branch>"
        ))
    first, second = ps
    if old != ZERO_OID and first != old:
        return Decision(False, "the merge's first parent is not the current main")
    if second not in verified:
        return Decision(False, (
            f"branch tip {second[:12]} has not passed scripts/verify.py; "
            "run it on the branch first"
        ))
    if tree_of(new, cwd) != tree_of(second, cwd):
        return Decision(False, (
            "the merge result differs from the verified branch tip, so the "
            "merged content was never tested; rebase the branch onto main, "
            "re-run scripts/verify.py, then merge"
        ))
    return Decision(True, "verified --no-ff merge")
