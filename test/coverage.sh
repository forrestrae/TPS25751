#!/bin/sh
# Line coverage of the native unit tests, per file in src/.
#
# Usage, from the library root:
#   test/coverage.sh            # run every native suite, print the report
#   test/coverage.sh --html     # also write .pio/coverage/index.html
#
# Needs gcovr (`brew install gcovr` or `pipx install gcovr`). Builds the
# [env:native-coverage] environment in platformio.ini, which compiles the same
# sources and tests as [env:native] with --coverage, from scratch on every run.
set -eu

cd "$(dirname "$0")/.."
BUILD=.pio/build/native-coverage

HTML=0
if [ "${1:-}" = "--html" ]; then
    HTML=1
fi

if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr not found; install it with 'brew install gcovr' or 'pipx install gcovr'" >&2
    exit 1
fi

# Apple clang writes LLVM-format counters, which need llvm-cov's gcov mode;
# GCC builds use the gcov that ships with GCC.
if xcrun --find llvm-cov >/dev/null 2>&1; then
    GCOV="xcrun llvm-cov gcov"
else
    GCOV="gcov"
fi

# Rebuild from scratch with an empty build cache. Counters accumulate across
# runs, and the shared cache in .pio/buildcache restores object files without
# the .gcno notes that gcov needs.
rm -rf "$BUILD"
CACHE=$(mktemp -d)
trap 'rm -rf "$CACHE"' EXIT
PLATFORMIO_BUILD_CACHE_DIR="$CACHE" pio test -e native-coverage

# Search only the library objects: the Unity and test objects are not reported,
# and Unity's own counters have no notes file next to them.
gcovr --root . --filter 'src/' --gcov-executable "$GCOV" \
    --sort uncovered-percent --txt-metric line "$BUILD/src"

if [ "$HTML" = "1" ]; then
    mkdir -p .pio/coverage
    gcovr --root . --filter 'src/' --gcov-executable "$GCOV" \
        --html-details .pio/coverage/index.html "$BUILD/src"
    echo "HTML report: .pio/coverage/index.html"
fi
