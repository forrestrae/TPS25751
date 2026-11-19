# PlatformIO extra script for [env:native-coverage]: build_flags reach the
# compiler only, so pass --coverage to the linker too. Without it the test
# binaries fail to link against the profiling runtime the instrumented
# objects call.
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

env.Append(LINKFLAGS=["--coverage"])  # noqa: F821
