#include <Stream.h>

#include "TPS25751TransmitSourceInfo.h"

namespace {

// mW / lsb_mW, rounded down, saturated at maxRaw. Saturating (not masking)
// keeps an over-range request at the field maximum instead of wrapping it.
uint16_t mWToRaw(uint32_t mW, uint16_t lsb_mW, uint16_t maxRaw)
{
    const uint32_t raw = mW / lsb_mW;
    return raw > maxRaw ? maxRaw : static_cast<uint16_t>(raw);
}

}  // namespace

// --- String conversion ------------------------------------------------------------

const char *TPS25751TransmitSourceInfo::toString(PortType val)
{
    switch (val) {
        case PortType::Managed: return "Managed Capability Port";
        case PortType::Guaranteed: return "Guaranteed Capability Port";
        default: return "Undefined";
    }
}

// --- Decode ---------------------------------------------------------------------

TPS25751TransmitSourceInfo::PortType TPS25751TransmitSourceInfo::portTypeUpper() const
{
    return static_cast<PortType>(extractBits(kPortTypeUpperBit, 1));
}

TPS25751TransmitSourceInfo::PortType TPS25751TransmitSourceInfo::portTypeLower() const
{
    return static_cast<PortType>(extractBits(kPortTypeLowerBit, 1));
}

bool TPS25751TransmitSourceInfo::dpsPort() const
{
    return extractBits(kDpsPortBit, 1);
}

uint16_t TPS25751TransmitSourceInfo::rawPortMaximumPdp0p5W() const
{
    return extractBits16(kPortMaximumPdp0p5WBit, 9);
}

uint32_t TPS25751TransmitSourceInfo::portMaximumPdp0p5W_mW() const
{
    return static_cast<uint32_t>(rawPortMaximumPdp0p5W()) * kHalfWattLsb_mW;
}

uint16_t TPS25751TransmitSourceInfo::rawPortGuaranteedPdp0p5W() const
{
    return extractBits16(kPortGuaranteedPdp0p5WBit, 9);
}

uint32_t TPS25751TransmitSourceInfo::portGuaranteedPdp0p5W_mW() const
{
    return static_cast<uint32_t>(rawPortGuaranteedPdp0p5W()) * kHalfWattLsb_mW;
}

uint8_t TPS25751TransmitSourceInfo::rawPortMaximumPdp() const
{
    return extractBits(kPortMaximumPdpBit, 8);
}

uint32_t TPS25751TransmitSourceInfo::portMaximumPdp_mW() const
{
    return static_cast<uint32_t>(rawPortMaximumPdp()) * kWattLsb_mW;
}

uint8_t TPS25751TransmitSourceInfo::rawPortPresentPdp() const
{
    return extractBits(kPortPresentPdpBit, 8);
}

uint32_t TPS25751TransmitSourceInfo::portPresentPdp_mW() const
{
    return static_cast<uint32_t>(rawPortPresentPdp()) * kWattLsb_mW;
}

uint8_t TPS25751TransmitSourceInfo::rawPortReportedPdp() const
{
    return extractBits(kPortReportedPdpBit, 8);
}

uint32_t TPS25751TransmitSourceInfo::portReportedPdp_mW() const
{
    return static_cast<uint32_t>(rawPortReportedPdp()) * kWattLsb_mW;
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751TransmitSourceInfo::setPortTypeUpper(PortType type)
{
    setBits(kPortTypeUpperBit, 1, static_cast<uint32_t>(type));
}

void TPS25751TransmitSourceInfo::setPortTypeLower(PortType type)
{
    setBits(kPortTypeLowerBit, 1, static_cast<uint32_t>(type));
}

void TPS25751TransmitSourceInfo::setDpsPort(bool enabled)
{
    setBits(kDpsPortBit, 1, enabled ? 1u : 0u);
}

void TPS25751TransmitSourceInfo::setRawPortMaximumPdp0p5W(uint16_t raw)
{
    setBits(kPortMaximumPdp0p5WBit, 9, raw);
}

void TPS25751TransmitSourceInfo::setRawPortGuaranteedPdp0p5W(uint16_t raw)
{
    setBits(kPortGuaranteedPdp0p5WBit, 9, raw);
}

void TPS25751TransmitSourceInfo::setRawPortMaximumPdp(uint8_t raw)
{
    setBits(kPortMaximumPdpBit, 8, raw);
}

void TPS25751TransmitSourceInfo::setRawPortPresentPdp(uint8_t raw)
{
    setBits(kPortPresentPdpBit, 8, raw);
}

void TPS25751TransmitSourceInfo::setRawPortReportedPdp(uint8_t raw)
{
    setBits(kPortReportedPdpBit, 8, raw);
}

void TPS25751TransmitSourceInfo::setPortMaximumPdp0p5W_mW(uint32_t mW)
{
    setRawPortMaximumPdp0p5W(mWToRaw(mW, kHalfWattLsb_mW, kMax9BitRaw));
}

void TPS25751TransmitSourceInfo::setPortGuaranteedPdp0p5W_mW(uint32_t mW)
{
    setRawPortGuaranteedPdp0p5W(mWToRaw(mW, kHalfWattLsb_mW, kMax9BitRaw));
}

void TPS25751TransmitSourceInfo::setPortMaximumPdp_mW(uint32_t mW)
{
    setRawPortMaximumPdp(static_cast<uint8_t>(mWToRaw(mW, kWattLsb_mW, kMax8BitRaw)));
}

void TPS25751TransmitSourceInfo::setPortPresentPdp_mW(uint32_t mW)
{
    setRawPortPresentPdp(static_cast<uint8_t>(mWToRaw(mW, kWattLsb_mW, kMax8BitRaw)));
}

void TPS25751TransmitSourceInfo::setPortReportedPdp_mW(uint32_t mW)
{
    setRawPortReportedPdp(static_cast<uint8_t>(mWToRaw(mW, kWattLsb_mW, kMax8BitRaw)));
}

// --- Debug ------------------------------------------------------------------------

namespace {

void printHex32(Stream &s, uint32_t value)
{
    // Zero-padded to 8 hex digits; Stream's HEX output drops leading zeros.
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        s.print(static_cast<unsigned int>((value >> shift) & 0xFu), HEX);
    }
}

void printPdp(Stream &s, const __FlashStringHelper *label, const __FlashStringHelper *unit,
              unsigned int raw, uint32_t mW)
{
    s.print(label);
    s.print(F(" ("));
    s.print(unit);
    s.print(F("): "));
    s.print(raw);
    s.print(F(" ("));
    s.print(static_cast<unsigned long>(mW));
    s.println(F(" mW)"));
}

}  // namespace

void TPS25751TransmitSourceInfo::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("TX_SOURCE_INFO (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::TRANSMIT_SOURCE_INFO), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    // The two 32-bit words, upper first, so the line reads like the TRM's
    // 8000000080000000h reset notation.
    s.print(F("Raw: 0x"));
    printHex32(s, extractBits32(32, 32));
    printHex32(s, extractBits32(0, 32));
    s.println();

    s.print(F("Port Type (bit 63): "));
    s.print(toString(portTypeUpper()));
    s.print(F(" ("));
    s.print(static_cast<uint8_t>(portTypeUpper()));
    s.println(F(")"));

    s.print(F("DPS Port (bit 62): "));
    s.println(dpsPort() ? F("Yes") : F("No"));

    printPdp(s, F("Port Maximum PDP"), F("0.5 W units"), rawPortMaximumPdp0p5W(), portMaximumPdp0p5W_mW());
    printPdp(s, F("Port Guaranteed PDP"), F("0.5 W units"), rawPortGuaranteedPdp0p5W(), portGuaranteedPdp0p5W_mW());

    s.print(F("Port Type (bit 31): "));
    s.print(toString(portTypeLower()));
    s.print(F(" ("));
    s.print(static_cast<uint8_t>(portTypeLower()));
    s.println(F(")"));

    printPdp(s, F("Port Maximum PDP"), F("1 W units"), rawPortMaximumPdp(), portMaximumPdp_mW());
    printPdp(s, F("Port Present PDP"), F("1 W units"), rawPortPresentPdp(), portPresentPdp_mW());
    printPdp(s, F("Port Reported PDP"), F("1 W units"), rawPortReportedPdp(), portReportedPdp_mW());

    const uint32_t reservedUpper = extractBits32(32, 32) & kReservedMaskUpper;
    const uint32_t reservedLower = extractBits32(0, 32) & kReservedMaskLower;
    if (reservedUpper != 0 || reservedLower != 0)
    {
        s.print(F("Reserved bits set: 0x"));
        printHex32(s, reservedUpper);
        printHex32(s, reservedLower);
        s.println();
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751TransmitSourceInfo::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_SOURCE_INFO.size;
    if (!isValidSize(expectedSize)) return false;

    const uint32_t reservedUpper = extractBits32(32, 32) & kReservedMaskUpper;
    const uint32_t reservedLower = extractBits32(0, 32) & kReservedMaskLower;
    if (reservedUpper != 0 || reservedLower != 0)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TX_SOURCE_INFO: reserved bits set (upper 0x%08lX, lower 0x%08lX)",
                       static_cast<unsigned long>(reservedUpper), static_cast<unsigned long>(reservedLower));
        return false;
    }

    // TRM: DPSPort "If set, then PortType shall be 0 (Managed Capability Port)".
    if (dpsPort() && portTypeUpper() != PortType::Managed)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TX_SOURCE_INFO: DPS Port set but Port Type (bit 63) is not Managed");
        return false;
    }

    // Inferred from the TRM field descriptions; the TRM does not state this rule.
    const uint16_t maximum = rawPortMaximumPdp0p5W();
    const uint16_t guaranteed = rawPortGuaranteedPdp0p5W();
    if (maximum != 0 && guaranteed > maximum)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TX_SOURCE_INFO: Port Guaranteed PDP (%u) exceeds Port Maximum PDP (%u)",
                       static_cast<unsigned>(guaranteed), static_cast<unsigned>(maximum));
        return false;
    }

    return true;
}
