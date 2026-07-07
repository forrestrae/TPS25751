#include "TPS25751CustomerUse.h"

#include <cstdio>
#include <cstring>

uint64_t TPS25751CustomerUse::value() const {
    return extractBits64(0, 64);
}

uint8_t TPS25751CustomerUse::byteAt(size_t index) const {
    if (!isValid() || index >= _len) return 0;
    return _raw[index];
}

void TPS25751CustomerUse::setValue(uint64_t v) {
    if (!isValid()) return;
    for (size_t i = 0; i < _len && i < 8; ++i) {
        _raw[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

void TPS25751CustomerUse::setBytes(const uint8_t* src, size_t len) {
    if (!isValid() || !src) return;
    size_t n = len < 8 ? len : 8;
    n = n < _len ? n : _len;
    memcpy(_raw, src, n);
}

void TPS25751CustomerUse::debugPrint(Stream &s) const {
    s.println(F("========================================"));
    s.print(F("CUSTOMER_USE (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::CUSTOMER_USE), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    if (!isValid()) {
        s.println(F("  <invalid>"));
        s.println(F("========================================"));
        return;
    }

    debugDumpRaw(s, "Raw");

    // Format the 64-bit value as zero-padded hex manually: not every Print
    // implementation (Arduino/Teensy or the native test stub) supports a
    // 64-bit print(..., HEX) overload with leading zeros preserved.
    const uint64_t v = value();
    char buf[17];
    snprintf(buf, sizeof(buf), "%08lX%08lX",
              static_cast<unsigned long>(v >> 32),
              static_cast<unsigned long>(v & 0xFFFFFFFFUL));
    s.print(F("Value: 0x"));
    s.println(buf);

    s.println(F("========================================"));
}

bool TPS25751CustomerUse::isSemanticallyValid() const {
    if (!isValid()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::CUSTOMER_USE.size;
    return isValidSize(expectedSize);
}
