#pragma once

// Stdout capture helpers for the native Unity suites.
//
// The native Stream stub writes to stdout, so a test that wants to check what a
// debugPrint() or other print routine produced redirects stdout to a temporary
// file for the duration of the call and reads the file back. captureStdout()
// does that for any callable, and captureDebugPrint() wraps it for the common
// case of a register's debugPrint().
//
// If tmpfile() fails, captureStdout() fails the running test with
// TEST_FAIL_MESSAGE rather than returning an empty string, so a broken capture
// cannot make an empty-output assertion pass by accident.

#include <cstdio>
#include <string>
#include <unistd.h>

#include <unity.h>

#include "TPS25751Register.h"

// Runs fn with stdout redirected to a temporary file and returns what it wrote.
template <typename Fn>
std::string captureStdout(Fn &&fn)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    if (tmp == nullptr)
    {
        TEST_FAIL_MESSAGE("captureStdout: tmpfile() failed");
        return std::string();
    }
    const int saved = dup(fileno(stdout));
    dup2(fileno(tmp), fileno(stdout));

    fn();

    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    std::string out;
    rewind(tmp);
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), tmp)) > 0) out.append(buf, n);
    fclose(tmp);
    return out;
}

// Returns what reg.debugPrint() prints to the stdout-backed native Stream stub.
inline std::string captureDebugPrint(const TPS25751Register &reg)
{
    return captureStdout([&] { reg.debugPrint(); });
}
