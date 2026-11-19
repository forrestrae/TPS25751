#!/usr/bin/env python3
"""Print the release level (major, minor or patch) implied by a commit range.

Usage: release_level.py <rev-range>

A breaking marker ("type!:" or a "BREAKING CHANGE:" footer) gives major, any
feat commit gives minor, anything else gives patch. Merge commits and the
release bot's chore(release) commits are ignored. Subject-line tokens such as
"#minor" have no effect. The rules live in scripts/workflow_common.py.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workflow_common as wc  # noqa: E402


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    messages = [message for _, message in wc.commit_messages(argv[1])]
    print(wc.release_level(messages))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
