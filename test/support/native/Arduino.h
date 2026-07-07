#pragma once

// Minimal Arduino API stub for the PlatformIO *native* test environment.
//
// The library's register decode/encode logic is platform-independent, but the
// sources reference a handful of Arduino symbols (Stream, Serial, F(),
// millis(), strncpy_P). This header provides just enough of that surface to
// compile and run them on the host — it is NOT a general Arduino emulation.
// Output goes to stdout so debugPrint()/hexDump() remain usable in tests.
//
// Only the native env sees this file (via `-I test/support/native` in
// platformio.ini); hardware builds use the real Teensy core headers.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>

class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(string_literal))
#define PROGMEM

#define DEC 10
#define HEX 16

inline unsigned long millis() { return 0; }

inline char *strncpy_P(char *dest, const char *src, size_t n)
{
    return strncpy(dest, src, n);
}

class Stream
{
public:
    virtual ~Stream() {}

    size_t print(const char *s)
    {
        if (!s) return 0;
        fputs(s, stdout);
        return strlen(s);
    }
    size_t print(char c)
    {
        fputc(c, stdout);
        return 1;
    }
    size_t print(const __FlashStringHelper *s)
    {
        return print(reinterpret_cast<const char *>(s));
    }
    size_t print(int v, int base = DEC) { return printSigned(v, base); }
    size_t print(long v, int base = DEC) { return printSigned(v, base); }
    size_t print(unsigned int v, int base = DEC) { return printUnsigned(v, base); }
    size_t print(unsigned long v, int base = DEC) { return printUnsigned(v, base); }
    size_t print(unsigned long long v, int base = DEC)
    {
        return printUnsigned(static_cast<unsigned long>(v), base);
    }

    template <typename T>
    size_t println(T v)
    {
        size_t n = print(v);
        return n + println();
    }
    template <typename T>
    size_t println(T v, int base)
    {
        size_t n = print(v, base);
        return n + println();
    }
    size_t println()
    {
        fputc('\n', stdout);
        return 1;
    }

private:
    size_t printSigned(long v, int base)
    {
        int n = (base == HEX) ? printf("%lX", static_cast<unsigned long>(v))
                              : printf("%ld", v);
        return (n > 0) ? static_cast<size_t>(n) : 0;
    }
    size_t printUnsigned(unsigned long v, int base)
    {
        int n = (base == HEX) ? printf("%lX", v) : printf("%lu", v);
        return (n > 0) ? static_cast<size_t>(n) : 0;
    }
};

// Per-translation-unit stand-in for the global Serial object. Identity does
// not matter here — it is only ever used as an output sink / default argument.
static Stream Serial;
