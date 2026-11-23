#!/usr/bin/env python3
"""The checks behind scripts/verify.py. Exits 0 if the branch passes, 1 if not.

Usage: scripts/verify_gate.py

scripts/verify.py runs main's copy of this file against the topic branch's
working tree, so a change to these checks applies only after it reaches main.
Running this file directly checks the branch but never marks it verified; only
scripts/verify.py writes <git-common-dir>/verified-commits.

Checks, in order:

  1. every commit message in main..HEAD
  2. no merge commits on the branch (rebase instead)
  3. version fields in library.json / library.properties unchanged from main
  4. script unit tests, native unit tests, example build
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
    head = wc.git("rev-parse", "HEAD", cwd=root)

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
