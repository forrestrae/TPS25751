#!/usr/bin/env python3
"""Run main's copy of the gate on the current topic branch and stamp it verified.

Usage: scripts/verify.py

Run from a topic branch that is rebased onto main, with a clean working tree
(no modified, staged or untracked files), so the stamp describes exactly the
content that was tested.

The checks live in scripts/verify_gate.py, which agents may change on a topic
branch. This launcher never runs the branch's copy: it extracts
verify_gate.py and workflow_common.py from main into a temporary directory and
runs those against the branch's working tree. A branch therefore cannot be
verified by a gate it modified; a gate change applies to later branches once
the human has pushed it to main.

On success the HEAD commit is appended to <git-common-dir>/verified-commits,
which is what lets the reference-transaction hook accept a --no-ff merge of
this branch into main. This launcher is the only intended writer of that file.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import workflow_common as wc  # noqa: E402

GATE = "scripts/verify_gate.py"
GATE_SUPPORT = ("scripts/workflow_common.py",)


def fail(message: str) -> int:
    print(f"verify: FAILED: {message}", file=sys.stderr)
    return 1


def extract_gate(root: Path, dest: Path) -> tuple[Path, str]:
    """Write main's gate and its support modules into dest; return the gate path and its source."""
    main_sha = wc.git("rev-parse", wc.MAIN_REF, cwd=root)
    source = f"main {main_sha[:12]}"
    if not wc.git_ok("cat-file", "-e", f"{main_sha}:{GATE}", cwd=root):
        # Bootstrap only: the branch that introduces verify_gate.py has no copy on main to run.
        source = "this branch (main has no verify_gate.py yet; bootstrap run)"
        rev = "HEAD"
    else:
        rev = main_sha
    for path in (GATE, *GATE_SUPPORT):
        (dest / Path(path).name).write_text(wc.git("show", f"{rev}:{path}", cwd=root) + "\n")
    return dest / Path(GATE).name, source


def main() -> int:
    root = wc.repo_root()
    branch_ref = wc.git("symbolic-ref", "-q", "HEAD", check=False, cwd=root)
    if not branch_ref.startswith("refs/heads/"):
        return fail("HEAD is detached; check out the topic branch")
    branch = branch_ref[len("refs/heads/"):]
    err = wc.check_branch_name(branch)
    if err:
        return fail(err)

    status = wc.git("status", "--porcelain", "--untracked-files=all", cwd=root)
    if status:
        return fail("working tree is not clean; commit, stash (-u) or remove:\n" + status)

    head = wc.git("rev-parse", "HEAD", cwd=root)
    if not wc.is_ancestor(wc.MAIN_REF, head, cwd=root):
        return fail("branch is not based on the current main; run: git rebase main")
    if head == wc.git("rev-parse", wc.MAIN_REF, cwd=root):
        return fail("branch has no commits beyond main")

    with tempfile.TemporaryDirectory(prefix="verify-gate-") as tmp:
        gate, source = extract_gate(root, Path(tmp))
        print(f"verify: gate from {source}", flush=True)
        if subprocess.run([sys.executable, str(gate)], cwd=root).returncode != 0:
            return fail(f"gate from {source}")

    if wc.git("status", "--porcelain", "--untracked-files=all", cwd=root):
        return fail("the gate modified tracked or untracked files; investigate before merging")
    if wc.git("rev-parse", "HEAD", cwd=root) != head:
        return fail("HEAD moved while the gate ran")

    wc.record_verified(head, cwd=root)
    print(f"verify: PASSED: {branch} at {head[:12]} is verified.")
    print(f"verify: next: scripts/integrate.py {branch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
