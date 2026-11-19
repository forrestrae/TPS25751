#include <Stream.h>

#include "TPS25751PD3Config.h"

// --- Decode ---------------------------------------------------------------------

bool TPS25751PD3Config::supportPpsStatus() const
{
    return extractBits(kSupportPpsStatusBit, 1);
}

bool TPS25751PD3Config::supportGetRevision() const
{
    return extractBits(kSupportGetRevisionBit, 1);
}

bool TPS25751PD3Config::supportGetSourceInfo() const
{
    return extractBits(kSupportGetSourceInfoBit, 1);
}

bool TPS25751PD3Config::supportSinkCapExtended() const
{
    return extractBits(kSupportSinkCapExtendedBit, 1);
}

bool TPS25751PD3Config::supportBatteryStatus() const
{
    return extractBits(kSupportBatteryStatusBit, 1);
}

bool TPS25751PD3Config::supportBatteryCapabilities() const
{
    return extractBits(kSupportBatteryCapabilitiesBit, 1);
}

bool TPS25751PD3Config::supportSourceExtended() const
{
    return extractBits(kSupportSourceExtendedBit, 1);
}

bool TPS25751PD3Config::autoGetMidb() const
{
    return extractBits(kAutoGetMidbBit, 1);
}

bool TPS25751PD3Config::unchunkedSupported() const
{
    return extractBits(kUnchunkedSupportedBit, 1);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751PD3Config::setSupportPpsStatus(bool enabled)
{
    setBits(kSupportPpsStatusBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setSupportGetRevision(bool enabled)
{
    setBits(kSupportGetRevisionBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setSupportGetSourceInfo(bool enabled)
{
    setBits(kSupportGetSourceInfoBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setSupportSinkCapExtended(bool enabled)
{
    setBits(kSupportSinkCapExtendedBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setSupportBatteryStatus(bool enabled)
{
    setBits(kSupportBatteryStatusBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setSupportBatteryCapabilities(bool enabled)
{
    setBits(kSupportBatteryCapabilitiesBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setSupportSourceExtended(bool enabled)
{
    setBits(kSupportSourceExtendedBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setAutoGetMidb(bool enabled)
{
    setBits(kAutoGetMidbBit, 1, enabled ? 1u : 0u);
}

void TPS25751PD3Config::setUnchunkedSupported(bool enabled)
{
    setBits(kUnchunkedSupportedBit, 1, enabled ? 1u : 0u);
}

// --- Debug ------------------------------------------------------------------------

namespace {

void printEnabled(Stream &s, const __FlashStringHelper *label, bool value)
{
    s.print(label);
    s.println(value ? F("Enabled") : F("Disabled"));
}

}  // namespace

void TPS25751PD3Config::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("PD3_CONFIGURATION (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::PD3_CONFIGURATION), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    // Zero-padded to 8 hex digits so the value lines up with the TRM's
    // 00080010h notation; Stream's HEX output drops leading zeros.
    const uint32_t value = extractBits32(0, 32);
    s.print(F("Raw: 0x"));
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        s.print(static_cast<unsigned int>((value >> shift) & 0xFu), HEX);
    }
    s.println();

    printEnabled(s, F("Support PPS Status: "), supportPpsStatus());
    printEnabled(s, F("Support Get Revision: "), supportGetRevision());
    printEnabled(s, F("Support Get Source Info: "), supportGetSourceInfo());
    printEnabled(s, F("Support Sink Cap Extended: "), supportSinkCapExtended());
    printEnabled(s, F("Support Battery Status Message: "), supportBatteryStatus());
    printEnabled(s, F("Support Battery Capabilities Message: "), supportBatteryCapabilities());
    printEnabled(s, F("Support Source Extended Message: "), supportSourceExtended());
    printEnabled(s, F("Auto Get MIDB: "), autoGetMidb());
    printEnabled(s, F("Unchunked Supported: "), unchunkedSupported());

    const uint32_t reserved = value & kReservedMask;
    if (reserved != 0)
    {
        s.print(F("Reserved bits set: 0x"));
        s.println(static_cast<unsigned long>(reserved), HEX);
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751PD3Config::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::PD3_CONFIGURATION.size;
    if (!isValidSize(expectedSize)) return false;

    const uint32_t reserved = extractBits32(0, 32) & kReservedMask;
    if (reserved != 0)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "PD3_CONFIGURATION: reserved bits set (0x%08lX)", static_cast<unsigned long>(reserved));
        return false;
    }

    return true;
}
