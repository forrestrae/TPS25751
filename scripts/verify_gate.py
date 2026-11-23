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
  4. script unit tests, PD capture fixtures, bench server tests, native unit
     tests, example build

The PD capture fixture check and the bench server tests run once their target
exists (test/support/pd_capture_fixture.py, tools/pd-bench/); until then each
prints a notice and is skipped.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import workflow_common as wc  # noqa: E402


@dataclass(frozen=True)
class Gate:
    name: str
    cmd: list[str]
    cwd: str = "."  # relative to the repository root
    requires: str | None = None  # skipped, with a notice, while this path does not exist


GATES = [
    Gate("script unit tests", [sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests", "-t", "scripts", "-q"]),
    Gate("PD capture fixtures", [sys.executable, "test/support/pd_capture_fixture.py", "--check"],
         requires="test/support/pd_capture_fixture.py"),
    Gate("bench server tests", ["uv", "run", "--extra", "dev", "pytest"],
         cwd="tools/pd-bench", requires="tools/pd-bench"),
    Gate("native unit tests", ["pio", "test", "-e", "native"]),
    Gate("example build", ["pio", "run", "-e", "example-read-registers"]),
]


def fail(message: str) -> int:
    print(f"verify: FAILED: {message}", file=sys.stderr)
    return 1


def run_gates(root: Path, gates: list[Gate] = GATES, run=subprocess.run) -> int:
    """Run each gate in order and stop at the first failure."""
    for gate in gates:
        if gate.requires and not (root / gate.requires).exists():
            print(f"verify: skipping {gate.name}: {gate.requires} does not exist yet", flush=True)
            continue
        print(f"verify: running {gate.name}: {' '.join(gate.cmd)}", flush=True)
        try:
            returncode = run(gate.cmd, cwd=root / gate.cwd).returncode
        except FileNotFoundError:
            return fail(f"{gate.name}: {gate.cmd[0]} is not installed or not on PATH")
        if returncode != 0:
            return fail(gate.name)
    return 0


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

    return run_gates(root)


if __name__ == "__main__":
    sys.exit(main())
