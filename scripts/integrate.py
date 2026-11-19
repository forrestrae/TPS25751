#!/usr/bin/env python3
"""Merge a verified topic branch into local main, archive it, and delete it.

Usage: scripts/integrate.py <branch>

Runs the repository's integrate step:

  git switch main
  git merge --no-ff <branch> -m "Merge branch '<branch>'"
  git tag archive/<branch> <branch>
  git branch -d <branch>

The --no-ff is what makes the merge commit exist; a plain merge of a branch
that is already ahead of main fast-forwards and silently drops -m. The
reference-transaction hook refuses the merge unless scripts/verify.py passed
on the branch tip. Pushing is left to the human.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workflow_common as wc  # noqa: E402


def run(*args: str) -> bool:
    print(f"integrate: git {' '.join(args)}", flush=True)
    return subprocess.run(["git", *args]).returncode == 0


def _worktree_with_main() -> str | None:
    """Path of the worktree that has main checked out, if any."""
    path = None
    for line in wc.git("worktree", "list", "--porcelain").splitlines():
        if line.startswith("worktree "):
            path = line[len("worktree "):]
        elif line == f"branch {wc.MAIN_REF}":
            return str(Path(path).resolve()) if path else None
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    branch = argv[1]
    err = wc.check_branch_name(branch)
    if err:
        print(f"integrate: {err}", file=sys.stderr)
        return 1
    if not wc.ref_exists(f"refs/heads/{branch}"):
        print(f"integrate: no local branch {branch!r}", file=sys.stderr)
        return 1
    tag = f"{wc.ARCHIVE_TAG_PREFIX}{branch}"
    if wc.ref_exists(f"refs/tags/{tag}"):
        print(f"integrate: tag {tag} already exists", file=sys.stderr)
        return 1
    if wc.git("status", "--porcelain", "--untracked-files=no"):
        print("integrate: working tree has uncommitted changes", file=sys.stderr)
        return 1

    tip = wc.git("rev-parse", f"refs/heads/{branch}")
    if tip not in wc.verified_commits():
        print(f"integrate: {branch} at {tip[:12]} has not passed scripts/verify.py", file=sys.stderr)
        return 1
    if not wc.is_ancestor(wc.MAIN_REF, tip):
        print(f"integrate: main has moved since {branch} was based on it; "
              f"rebase it onto main and re-run scripts/verify.py", file=sys.stderr)
        return 1

    main_worktree = _worktree_with_main()
    here = str(wc.repo_root().resolve())
    if main_worktree and main_worktree != here:
        print(f"integrate: main is checked out in {main_worktree}; run "
              f"scripts/integrate.py {branch} from there", file=sys.stderr)
        return 1

    steps = [
        ("switch", wc.MAIN_BRANCH),
        ("merge", "--no-ff", branch, "-m", f"Merge branch '{branch}'"),
        ("tag", tag, branch),
        ("branch", "-d", branch),
    ]
    for step in steps:
        if not run(*step):
            print("integrate: stopped; nothing after this step was run", file=sys.stderr)
            return 1

    print(f"integrate: done. To release, the human runs:\n"
          f"  git push origin main refs/tags/{tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
