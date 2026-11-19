#!/usr/bin/env python3
"""Run the full local gate on the current topic branch and stamp it verified.

Usage: scripts/verify.py

Run from a topic branch that is rebased onto main, with a clean working tree
(no modified, staged or untracked files), so the stamp describes exactly the
content that was tested. Checks, in order:

  1. branch name and every commit message in main..HEAD
  2. no merge commits on the branch (rebase instead)
  3. version fields in library.json / library.properties unchanged from main
  4. script unit tests, native unit tests, example build

On success the HEAD commit is appended to <git-common-dir>/verified-commits,
which is what lets the reference-transaction hook accept a --no-ff merge of
this branch into main. This script is the only intended writer of that file.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import workflow_common as wc  # noqa: E402

GATES = [
    ("script unit tests", [sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests", "-t", "scripts", "-q"]),
    ("native unit tests", ["pio", "test", "-e", "native"]),
    ("example build", ["pio", "run", "-e", "example-read-registers"]),
]


def fail(message: str) -> int:
    print(f"verify: FAILED: {message}", file=sys.stderr)
    return 1


def version_at(rev: str, root: Path) -> tuple[str, str]:
    manifest = json.loads(wc.git("show", f"{rev}:library.json", cwd=root))
    props = wc.git("show", f"{rev}:library.properties", cwd=root)
    match = re.search(r"^version=(.*)$", props, re.M)
    return manifest.get("version", ""), match.group(1).strip() if match else ""


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
    if wc.git("rev-list", "--merges", f"{wc.MAIN_REF}..{head}", cwd=root):
        return fail("branch contains merge commits; rebase it onto main instead")

    errors = [
        f"{sha[:12]}: {e}"
        for sha, message in wc.commit_messages(f"{wc.MAIN_REF}..{head}", cwd=root)
        if (e := wc.check_message(message))
    ]
    if errors:
        return fail("commit messages:\n  " + "\n  ".join(errors))

    if version_at(wc.MAIN_REF, root) != version_at(head, root):
        return fail("version fields changed; versions are bumped only by CI on release")

    for name, cmd in GATES:
        print(f"verify: running {name}: {' '.join(cmd)}", flush=True)
        if subprocess.run(cmd, cwd=root).returncode != 0:
            return fail(name)

    if wc.git("status", "--porcelain", "--untracked-files=all", cwd=root):
        return fail("the gate modified tracked or untracked files; investigate before merging")

    wc.record_verified(head, cwd=root)
    print(f"verify: PASSED: {branch} at {head[:12]} is verified.")
    print(f"verify: next: scripts/integrate.py {branch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
