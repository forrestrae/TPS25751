#!/usr/bin/env python3
"""Build and run the native test suites in parallel, outside PlatformIO.

Usage, from anywhere in the checkout:
  test/native_fast.py                       # every suite
  test/native_fast.py -f "*EepromUpdater"   # suites matching a pattern (repeatable)
  test/native_fast.py -v                    # print every suite's output
  test/native_fast.py -j 8                  # limit parallel jobs (default: CPU count)

A fast check to run while working. `pio test -e native` stays the gate that
verify.py and CI run; run it before finishing a branch.

The sources, build flags, and suite filter come from [env:native] in
platformio.ini, so the two cannot drift. Each library source and each suite is
compiled once into .pio/native-fast/ and rebuilt only when it, a header it
includes, or the flags change. Each suite is linked into its own program and
all programs run in parallel. Unity comes from PlatformIO's copy in
.pio/libdeps/native/Unity, which the first `pio test -e native` installs.
"""

from __future__ import annotations

import argparse
import configparser
import fnmatch
import hashlib
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UNITY_SRC = ROOT / ".pio/libdeps/native/Unity/src"
BUILD_ROOT = ROOT / ".pio/native-fast"
TEST_DIR = ROOT / "test"

# The defines PlatformIO passes to a native test build. include/TPS25751Register.h
# exposes the base class's bit helpers under PIO_UNIT_TESTING.
DEFINES = ["-DPIO_UNIT_TESTING", "-DUNIT_TEST", "-DUNITY_INCLUDE_CONFIG_H"]

# Unity writes through putchar and flushes after every line, so a suite that
# crashes still shows the tests that ran before the crash.
UNITY_CONFIG = """\
#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H
#include <stdio.h>
#define UNITY_OUTPUT_CHAR(c) putchar(c)
#define UNITY_OUTPUT_FLUSH() fflush(stdout)
#endif
"""

SUMMARY_RE = re.compile(r"^(\d+) Tests (\d+) Failures (\d+) Ignored", re.MULTILINE)


class Env:
    def __init__(self, sources: list[Path], flags: list[str], suite_filter: str):
        self.sources = sources
        self.flags = flags
        self.suite_filter = suite_filter


def read_env() -> Env:
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",),
                                       comment_prefixes=(";", "#"), strict=False)
    parser.read(ROOT / "platformio.ini")
    section = parser["env:native"]
    patterns = re.findall(r"\+<([^>]+)>", section.get("build_src_filter", ""))
    sources = []
    for pattern in patterns:
        matches = sorted((ROOT / "src").glob(pattern))
        if not matches:
            sys.exit(f"native_fast: build_src_filter names {pattern}, which matches nothing in src/")
        sources.extend(matches)
    flags = shlex.split(section.get("build_flags", ""))
    if any("${" in f for f in flags):
        sys.exit("native_fast: [env:native] build_flags uses ${...}, which this script does not expand")
    return Env(sources, flags, section.get("test_filter", "test_native/*").strip())


def find_suites(env: Env, patterns: list[str]) -> list[Path]:
    suites = sorted(p for p in TEST_DIR.glob(env.suite_filter) if p.is_dir() and p.name.startswith("test_"))
    if patterns:
        def wanted(p: Path) -> bool:
            rel = p.relative_to(TEST_DIR).as_posix()
            return any(fnmatch.fnmatch(rel, pat) or fnmatch.fnmatch(p.name, pat) for pat in patterns)
        suites = [s for s in suites if wanted(s)]
    return suites


def needs_build(obj: Path, dep: Path) -> bool:
    """True if obj is missing or older than any file the compiler recorded in dep."""
    if not obj.exists() or not dep.exists():
        return True
    built = obj.stat().st_mtime
    # Make syntax: "target.o: prereq prereq \<newline> prereq". No path in this
    # repository contains a space, so a whitespace split is enough.
    _, _, prereqs = dep.read_text().replace("\\\n", " ").partition(":")
    for name in prereqs.split():
        path = Path(name)
        if not path.is_absolute():
            path = ROOT / path
        try:
            if path.stat().st_mtime > built:
                return True
        except FileNotFoundError:
            return True
    return False


def compile_job(src: Path, obj: Path, args: list[str]) -> tuple[Path, str | None]:
    dep = obj.with_suffix(".d")
    if not needs_build(obj, dep):
        return src, None
    obj.parent.mkdir(parents=True, exist_ok=True)
    if src.suffix == ".c":
        # PlatformIO passes a C++ -std flag from build_flags to C++ files only.
        compiler = os.environ.get("CC", "gcc")
        args = [a for a in args if not (a.startswith("-std=") and "++" in a)]
    else:
        compiler = os.environ.get("CXX", "g++")
    # A path relative to the root makes __FILE__, and so Unity's failure lines,
    # read as they do under pio test.
    cmd = [compiler, *args, "-MMD", "-MF", str(dep), "-c", str(src.relative_to(ROOT)), "-o", str(obj)]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        obj.unlink(missing_ok=True)
        return src, result.stderr or result.stdout or "compiler failed"
    return src, None


def link_job(program: Path, objs: list[Path]) -> str | None:
    if program.exists():
        built = program.stat().st_mtime
        if all(o.stat().st_mtime <= built for o in objs):
            return None
    cmd = [os.environ.get("CXX", "g++"), "-o", str(program), *map(str, objs)]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return None if result.returncode == 0 else (result.stderr or "link failed")


def run_job(program: Path) -> tuple[int, str, float]:
    start = time.monotonic()
    result = subprocess.run([str(program)], cwd=ROOT, capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr, time.monotonic() - start


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-f", "--filter", action="append", default=[],
                    help="suite pattern, such as test_native/test_TPS25751Status or *Pdo (repeatable)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("-v", "--verbose", action="store_true", help="print every suite's output")
    opts = ap.parse_args(argv)

    if not (UNITY_SRC / "unity.c").exists():
        sys.exit(f"native_fast: Unity not found in {UNITY_SRC.relative_to(ROOT)}; "
                 "run `pio test -e native` once to install it")

    env = read_env()
    suites = find_suites(env, opts.filter)
    if not suites:
        sys.exit("native_fast: no suite matches")

    # Objects live under a directory named for the flags, so a flag change
    # rebuilds everything without deleting the previous build.
    key = hashlib.sha256(" ".join(env.flags + DEFINES).encode()).hexdigest()[:12]
    build = BUILD_ROOT / key
    config_dir = build / "unity_config"
    config_dir.mkdir(parents=True, exist_ok=True)
    config = config_dir / "unity_config.h"
    if not config.exists() or config.read_text() != UNITY_CONFIG:
        config.write_text(UNITY_CONFIG)

    base = [*DEFINES, *env.flags, "-Iinclude", "-Isrc", f"-I{UNITY_SRC}", f"-I{config_dir}", "-Itest"]
    started = time.monotonic()

    jobs = [(UNITY_SRC / "unity.c", build / "unity/unity.o", base)]
    jobs += [(s, build / "src" / (s.stem + ".o"), base) for s in env.sources]
    suite_objs: dict[Path, list[Path]] = {}
    for suite in suites:
        args = base + [f"-I{suite.relative_to(ROOT)}"]
        files = sorted(suite.glob("*.cpp")) + sorted(suite.glob("*.c"))
        suite_objs[suite] = [build / "test" / suite.name / (f.stem + ".o") for f in files]
        jobs += [(f, o, args) for f, o in zip(files, suite_objs[suite])]

    with ThreadPoolExecutor(opts.jobs) as pool:
        errors = [(src, err) for src, err in pool.map(lambda j: compile_job(*j), jobs) if err]
    if errors:
        for src, err in errors:
            print(f"--- compile error: {src.relative_to(ROOT)}\n{err}", file=sys.stderr)
        print(f"native_fast: {len(errors)} file(s) failed to compile", file=sys.stderr)
        return 1
    compiled = time.monotonic()

    shared = [build / "unity/unity.o"] + [build / "src" / (s.stem + ".o") for s in env.sources]

    def link_and_run(suite: Path):
        program = build / "bin" / suite.name
        program.parent.mkdir(parents=True, exist_ok=True)
        err = link_job(program, suite_objs[suite] + shared)
        if err:
            return suite, None, f"link error:\n{err}", 0.0
        code, output, seconds = run_job(program)
        return suite, code, output, seconds

    with ThreadPoolExecutor(opts.jobs) as pool:
        results = list(pool.map(link_and_run, suites))

    total = failures = ignored = 0
    failed = []
    for suite, code, output, seconds in results:
        name = suite.relative_to(TEST_DIR).as_posix()
        summary = SUMMARY_RE.search(output or "")
        if summary:
            total += int(summary.group(1))
            failures += int(summary.group(2))
            ignored += int(summary.group(3))
        ok = code == 0 and summary is not None and summary.group(2) == "0"
        if not ok:
            failed.append(name)
        if opts.verbose:
            print(output.rstrip())
        elif not ok and output.strip():
            # The failures and Unity's summary; -v shows the passing lines too.
            print("\n".join(l for l in output.rstrip().splitlines() if not l.endswith(":PASS")))
        if ok:
            status = "PASSED"
        elif code is not None and code < 0:
            status = f"CRASHED (signal {-code})"
        else:
            status = "FAILED"
        print(f"{name:<55} {status:<8} {seconds:6.2f}s")

    finished = time.monotonic()
    print(f"{len(suites)} suites, {total} tests, {failures} failures, {ignored} ignored; "
          f"build {compiled - started:.1f}s, link and run {finished - compiled:.1f}s")
    if failed:
        print("Failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
