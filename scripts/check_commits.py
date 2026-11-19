#!/usr/bin/env python3
"""Lint commit messages and branch names against the repository convention.

Usage:
  check_commits.py msg-file <path>   lint one message file (commit-msg hook)
  check_commits.py range <rev-range> lint every commit in a range (verify, CI)
  check_commits.py branch <name>     lint a topic-branch name

Exits 1 and lists each offender when a check fails. The rules live in
scripts/workflow_common.py.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workflow_common as wc  # noqa: E402


def lint_range(rev_range: str) -> list[str]:
    errors = []
    for sha, message in wc.commit_messages(rev_range):
        err = wc.check_message(message)
        if err:
            errors.append(f"{sha[:12]}: {err}")
    return errors


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] not in ("msg-file", "range", "branch"):
        print(__doc__, file=sys.stderr)
        return 2
    mode, arg = argv[1], argv[2]

    if mode == "msg-file":
        comment_char = wc.git("config", "--get", "core.commentChar", check=False) or "#"
        message = wc.clean_message(Path(arg).read_text(), comment_char)
        err = wc.check_message(message, allow_autosquash=True)
        errors = [err] if err else []
    elif mode == "range":
        errors = lint_range(arg)
    else:
        err = wc.check_branch_name(arg)
        errors = [err] if err else []

    for err in errors:
        print(f"check_commits: {err}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
