#pragma once

// Minimal Arduino API stub for the PlatformIO *native* test environment.
//
// The library's register decode/encode logic is platform-independent, but the
// sources reference a handful of Arduino symbols (Stream, Serial, F(),
// millis(), delay(), digitalRead(), strncpy_P). This header provides just
// enough of that surface to compile and run them on the host; it is not a
// general Arduino emulation. Wire.h beside it provides a TwoWire fake.
// Output goes to stdout so debugPrint()/hexDump() remain usable in tests; a
// test that wants the output instead uses the Stream double in FakeStream.h.
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

// A controllable clock. millis() and micros() read it; delay() and
// delayMicroseconds() advance it and return at once. Tests set or read
// fake_arduino::g_nowUs directly. Sources that never call delay() see a clock
// that stays at 0, as the stub did before the clock existed.
namespace fake_arduino {
inline uint64_t g_nowUs = 0;
inline int g_pinLevel[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
inline void reset()
{
    g_nowUs = 0;
    for (int &level : g_pinLevel) level = 1;
}
}  // namespace fake_arduino

// 32-bit and wrapping, as on the Teensy 4, where both return uint32_t. A test
// can start the clock just below a wrap to check elapsed-time arithmetic.
inline uint32_t millis() { return static_cast<uint32_t>(fake_arduino::g_nowUs / 1000); }
inline uint32_t micros() { return static_cast<uint32_t>(fake_arduino::g_nowUs); }
inline void delay(unsigned long ms) { fake_arduino::g_nowUs += static_cast<uint64_t>(ms) * 1000; }
inline void delayMicroseconds(unsigned int us) { fake_arduino::g_nowUs += us; }

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline void pinMode(uint8_t, uint8_t) {}
// Pin levels come from fake_arduino::g_pinLevel (default HIGH).
inline int digitalRead(uint8_t pin) { return fake_arduino::g_pinLevel[pin & 63]; }

inline char *strncpy_P(char *dest, const char *src, size_t n)
{
    return strncpy(dest, src, n);
}

// Stream follows the Arduino Print and Stream contract as far as the library
// uses it: every print() ends in the virtual write(uint8_t), and the input side
// is available(), read(), and peek(). The base class writes to stdout and has
// no input, which is what the global Serial stand-in needs. FakeStream.h
// overrides write() to capture output and the input calls to feed a reader.
class Stream
{
public:
    virtual ~Stream() {}

    virtual size_t write(uint8_t c)
    {
        fputc(c, stdout);
        return 1;
    }
    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t n = 0;
        for (size_t i = 0; i < size; ++i) n += write(buffer[i]);
        return n;
    }
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual void flush() {}

    size_t print(const char *s)
    {
        if (!s) return 0;
        return write(reinterpret_cast<const uint8_t *>(s), strlen(s));
    }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
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
    size_t println() { return write(static_cast<uint8_t>('\n')); }

private:
    size_t printSigned(long v, int base)
    {
        char buf[24];
        int n = (base == HEX) ? snprintf(buf, sizeof(buf), "%lX", static_cast<unsigned long>(v))
                              : snprintf(buf, sizeof(buf), "%ld", v);
        return (n > 0) ? write(reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(n)) : 0;
    }
    size_t printUnsigned(unsigned long v, int base)
    {
        char buf[24];
        int n = (base == HEX) ? snprintf(buf, sizeof(buf), "%lX", v)
                              : snprintf(buf, sizeof(buf), "%lu", v);
        return (n > 0) ? write(reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(n)) : 0;
    }
};

// Per-translation-unit stand-in for the global Serial object. Identity does
// not matter here — it is only ever used as an output sink / default argument.
static Stream Serial;
