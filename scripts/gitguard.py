#!/usr/bin/env python3
"""Decision logic for the reference-transaction and pre-push git hooks.

Usage (called by the wrappers in .githooks/):
  gitguard.py reference-transaction <state>   ref updates on stdin
  gitguard.py pre-push <remote> <url>         push refs on stdin

reference-transaction: in the "prepared" state, refuses any update of local
main that workflow_common.main_update_decision() does not allow.

pre-push: refuses every push from a coding-agent shell (Claude Code or Codex;
see workflow_common.detect_agent), since only the human pushes. For a human push of main, re-checks every new
first-parent commit against the same main-branch rule.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workflow_common as wc  # noqa: E402


def _current_branch_ref() -> str:
    return wc.git("symbolic-ref", "-q", "HEAD", check=False)


def reference_transaction(state: str, lines: list[str]) -> int:
    if state != "prepared":
        return 0
    head_ref = None
    for line in lines:
        parts = line.split()
        if len(parts) != 3:
            continue
        old, new, ref = parts
        if old.startswith("ref:") or new.startswith("ref:"):
            # Symbolic-ref change (for example `git switch` moving HEAD);
            # no branch tip moves.
            continue
        if ref == "HEAD":
            # A commit on a checked-out branch may be reported through HEAD.
            head_ref = head_ref if head_ref is not None else _current_branch_ref()
            if head_ref != wc.MAIN_REF:
                continue
        elif ref != wc.MAIN_REF:
            continue
        decision = wc.main_update_decision(old, new)
        if not decision.allowed:
            print(f"gitguard: refusing to update main: {decision.reason}", file=sys.stderr)
            return 1
    return 0


def pre_push(lines: list[str], env: dict[str, str]) -> int:
    agent = wc.detect_agent(env)
    if agent:
        print(f"gitguard: push refused from a {agent} shell; pushing to a remote is done "
              "only by the human, at the end of the workflow. Agents stop after "
              "scripts/integrate.py", file=sys.stderr)
        return 1

    for line in lines:
        parts = line.split()
        if len(parts) != 4:
            continue
        _local_ref, local_oid, remote_ref, remote_oid = parts
        if remote_ref != wc.MAIN_REF:
            continue
        if local_oid == wc.ZERO_OID:
            print("gitguard: deleting remote main is not allowed", file=sys.stderr)
            return 1
        base = remote_oid if remote_oid != wc.ZERO_OID else None
        rev_range = f"{base}..{local_oid}" if base else local_oid
        new_commits = wc.git("rev-list", "--first-parent", "--reverse", rev_range).split()
        verified = wc.verified_commits()
        previous = base or wc.ZERO_OID
        for sha in new_commits:
            decision = wc.main_update_decision(previous, sha, verified=verified)
            if not decision.allowed:
                print(f"gitguard: refusing to push main: commit {sha[:12]}: {decision.reason}",
                      file=sys.stderr)
                return 1
            previous = sha
    return 0


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[1] == "reference-transaction" and len(argv) == 3:
        return reference_transaction(argv[2], sys.stdin.read().splitlines())
    if len(argv) >= 2 and argv[1] == "pre-push":
        return pre_push(sys.stdin.read().splitlines(), dict(os.environ))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
