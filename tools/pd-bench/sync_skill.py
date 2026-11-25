#!/usr/bin/env python3
"""Copy the tps25751-pd-verify skill into a downstream project for Codex.

Codex reads skills from ``<project>/.agents/skills/``. This library links its own
``.agents/skills/tps25751-pd-verify`` to the plugin's skill directory, but a project that
consumes the library has no such checkout, so it gets a copy. Run this script from a
library checkout at the tag the project pins, so the skill matches the bench server that
tag ships:

    python3 tools/pd-bench/sync_skill.py --dest <project>

The copy replaces ``<project>/.agents/skills/tps25751-pd-verify`` as a whole, so files
removed from the skill disappear from the copy too. It records where it came from in
``.synced-from.json`` inside the copy. An existing directory without that file, and a
symlink, are left alone unless ``--force`` is given: either was put there by hand. A
``.agents`` or ``.agents/skills`` that links into the library is refused even with
``--force``. The old copy is moved aside and removed only after the new one is in place.

Standard library only. Exit status: 0 written (or would be, with ``--dry-run``), 2 refused.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

SKILL_NAME = "tps25751-pd-verify"
LIBRARY_ROOT = Path(__file__).resolve().parents[2]
SOURCE = LIBRARY_ROOT / "plugins" / "tps25751-bench" / "skills" / SKILL_NAME
MARKER = ".synced-from.json"
MARKER_FORMAT = "tps25751-skill-sync"
IGNORED = {"__pycache__", ".DS_Store", MARKER}


class SyncError(Exception):
    """The copy was refused; the message says why and what to do."""


def skill_files(source: Path) -> list[Path]:
    """The skill's files, relative to ``source``, in a stable order."""
    if not (source / "SKILL.md").is_file():
        raise SyncError(f"{source} has no SKILL.md; run this script from a library checkout")
    files = []
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if any(part in IGNORED for part in relative.parts) or not path.is_file():
            continue
        files.append(relative)
    return files


def library_origin(root: Path) -> dict:
    """The library version from library.json and, when git can tell, the commit and tag."""
    origin: dict = {}
    try:
        origin["library_version"] = json.loads((root / "library.json").read_text(encoding="utf-8"))["version"]
    except (OSError, ValueError, KeyError, TypeError):
        origin["library_version"] = None
    for key, args in (("commit", ["rev-parse", "HEAD"]), ("describe", ["describe", "--tags", "--always", "--dirty"])):
        try:
            done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, timeout=10)
            origin[key] = done.stdout.strip() if done.returncode == 0 and done.stdout.strip() else None
        except (OSError, subprocess.SubprocessError):
            origin[key] = None
    return origin


def target_for(project: Path) -> Path:
    return project / ".agents" / "skills" / SKILL_NAME


def check_target(project: Path, target: Path, source: Path, force: bool) -> None:
    if not project.is_dir():
        raise SyncError(f"{project} is not a directory; --dest names the downstream project's root")
    library = source.resolve().parents[3]
    if project.resolve() == library:
        raise SyncError(f"{project} is the library itself, whose .agents/skills entry is a symlink to the "
                        "plugin; nothing to copy")
    # A linked .agents or .agents/skills that leads into the library would make the copy
    # write into, and replace, the library's own files. --force does not override this.
    for linked in (project / ".agents", project / ".agents" / "skills"):
        if linked.exists() and linked.resolve().is_relative_to(library):
            raise SyncError(f"{linked} resolves into the library ({linked.resolve()}); replace the link "
                            "with a directory first")
    if target.is_symlink():
        # Replacing the link removes only the link, never what it points at.
        if not force:
            raise SyncError(f"{target} is a symlink (to {os.readlink(target)}); remove it or pass --force "
                            "to replace it with a copy")
    elif target.exists():
        if not target.is_dir():
            raise SyncError(f"{target} exists and is not a directory")
        if not (target / MARKER).is_file() and not force:
            raise SyncError(f"{target} exists and was not written by this script (no {MARKER}); "
                            "pass --force to replace it")


def sync(project: Path, source: Path = SOURCE, force: bool = False, dry_run: bool = False,
         origin: dict | None = None) -> dict:
    """Write the copy and return what was written."""
    project = project.expanduser()
    target = target_for(project)
    files = skill_files(source)
    check_target(project, target, source, force)
    hashes = {str(f): hashlib.sha256((source / f).read_bytes()).hexdigest() for f in files}
    record = {"format": MARKER_FORMAT, "version": 1, "skill": SKILL_NAME,
              "origin": origin if origin is not None else library_origin(source.parents[3]),
              "files": hashes}
    result = {"target": str(target), "files": [str(f) for f in files], "dry_run": dry_run}
    if dry_run:
        return result
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = target.with_name(f".{SKILL_NAME}.sync-{os.getpid()}")
    previous = target.with_name(f".{SKILL_NAME}.old-{os.getpid()}")
    for leftover in (staging, previous):
        _remove(leftover)
    staging.mkdir()
    try:
        for f in files:
            (staging / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / f, staging / f)
        (staging / MARKER).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        # The old copy (or link) moves aside first and is removed only once the new copy
        # is in place, so a failure at any point leaves one of the two at the target.
        had_previous = target.exists() or target.is_symlink()
        if had_previous:
            target.rename(previous)
        try:
            staging.rename(target)
        except OSError:
            if had_previous:
                previous.rename(target)
            raise
        _remove(previous)
    finally:
        _remove(staging)
    return result


def _remove(path: Path) -> None:
    """Remove a file, a link (never what it points at), or a directory tree, if present."""
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def main(argv: list[str] | None = None, source: Path = SOURCE) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dest", required=True, type=Path,
                        help="root of the downstream project; the copy goes to .agents/skills/" + SKILL_NAME)
    parser.add_argument("--force", action="store_true",
                        help="replace a symlink or a directory this script did not write")
    parser.add_argument("--dry-run", action="store_true", help="list the files and write nothing")
    args = parser.parse_args(argv)
    try:
        result = sync(args.dest, source, force=args.force, dry_run=args.dry_run)
    except SyncError as exc:
        print(f"sync_skill: {exc}", file=sys.stderr)
        return 2
    verb = "would write" if result["dry_run"] else "wrote"
    print(f"sync_skill: {verb} {len(result['files'])} files to {result['target']}")
    for name in result["files"]:
        print(f"  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
