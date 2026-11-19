#include <Stream.h>

#include "TPS25751Version.h"

uint32_t TPS25751Version::rawVersion() const
{
    return extractBits32(0, 32);
}

uint16_t TPS25751Version::versionMajor() const
{
    return extractBits16(16, 16);
}

uint8_t TPS25751Version::versionMinor() const
{
    return extractBits(8, 8);
}

uint8_t TPS25751Version::versionRevision() const
{
    return extractBits(0, 8);
}

namespace {

// Print value as exactly `digits` uppercase hex digits, zero-padded.
void printHexPadded(Stream &s, uint32_t value, uint8_t digits)
{
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
    {
        s.print(static_cast<unsigned int>((value >> shift) & 0xFu), HEX);
    }
}

}  // namespace

void TPS25751Version::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("VERSION (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::VERSION), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("Version (hex VVVV.MM.RR): "));
    printHexPadded(s, versionMajor(), 4);
    s.print(F("."));
    printHexPadded(s, versionMinor(), 2);
    s.print(F("."));
    printHexPadded(s, versionRevision(), 2);
    s.println();

    s.print(F("Raw: 0x"));
    printHexPadded(s, rawVersion(), 8);
    s.println();

    s.println(F("========================================"));
}

bool TPS25751Version::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::VERSION.size;
    if (!isValidSize(expectedSize)) return false;

    return true;
}
