"""Throwaway git repositories for the workflow script tests."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))


def clean_env() -> dict[str, str]:
    """Environment with no inherited GIT_* variables and a fixed identity."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update({
        "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid",
        "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.invalid",
        "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
    })
    return env


class TempRepo:
    def __init__(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.path = Path(self._dir.name).resolve() / "repo"
        self.path.mkdir()
        self.env = clean_env()
        self.git("init", "-q", "-b", "main")
        (self.path / "f").write_text("0\n")
        self.git("add", "f")
        self.git("commit", "-q", "-m", "chore: initial")

    def git(self, *args: str) -> str:
        return subprocess.run(["git", *args], cwd=self.path, env=self.env,
                              capture_output=True, text=True, check=True).stdout.strip()

    def commit(self, message: str, content: str | None = None, name: str = "f") -> str:
        (self.path / name).write_text(content if content is not None else message + "\n")
        self.git("add", name)
        self.git("commit", "-q", "-m", message)
        return self.git("rev-parse", "HEAD")

    def merge_commit(self, first: str, second: str, message: str = "Merge branch 'x'") -> str:
        tree = self.git("rev-parse", f"{second}^{{tree}}")
        return self.git("commit-tree", tree, "-p", first, "-p", second, "-m", message)

    def cleanup(self) -> None:
        self._dir.cleanup()
