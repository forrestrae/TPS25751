"""PlatformIO pre-script: pass the library version to the bench console.

The release tooling keeps the version in library.json and library.properties
only, so the sources have no constant for it. This script reads library.json and
defines TPS25751_LIBRARY_VERSION, which TPS25751BenchConsole reports in its
HELLO reply. Without the definition the console reports "unknown".

A project that uses the library as a dependency can set the same macro in its
own build flags: -D TPS25751_LIBRARY_VERSION=\\"3.1.0\\"
"""

import json
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

manifest = os.path.join(env.subst("$PROJECT_DIR"), "library.json")  # noqa: F821
with open(manifest, encoding="utf-8") as handle:
    version = json.load(handle)["version"]

env.Append(CPPDEFINES=[("TPS25751_LIBRARY_VERSION", env.StringifyMacro(version))])  # noqa: F821
