#include <Stream.h>

#include "TPS25751TransmitSinkCapsExtended.h"

namespace {

void printHex8(Stream &s, uint8_t value)
{
    // Zero-padded to 2 hex digits; Stream's HEX output drops leading zeros.
    s.print(static_cast<unsigned int>((value >> 4) & 0xFu), HEX);
    s.print(static_cast<unsigned int>(value & 0xFu), HEX);
}

void printHex16(Stream &s, uint16_t value)
{
    printHex8(s, static_cast<uint8_t>(value >> 8));
    printHex8(s, static_cast<uint8_t>(value & 0xFFu));
}

void printByteField(Stream &s, const __FlashStringHelper *label, uint8_t value)
{
    s.print(label);
    s.print(F("0x"));
    printHex8(s, value);
    s.print(F(" ("));
    s.print(value);
    s.println(F(")"));
}

}  // namespace

// --- Decode ---------------------------------------------------------------------

uint8_t TPS25751TransmitSinkCapsExtended::skedbVersion() const
{
    return extractBits(kSkedbVersionBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::loadStep() const
{
    return extractBits(kLoadStepBit, 8);
}

uint16_t TPS25751TransmitSinkCapsExtended::sinkLoadCharacteristics() const
{
    return extractBits16(kSinkLoadCharacteristicsBit, 16);
}

uint8_t TPS25751TransmitSinkCapsExtended::compliance() const
{
    return extractBits(kComplianceBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::touchTemperature() const
{
    return extractBits(kTouchTemperatureBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::batteryInfo() const
{
    return extractBits(kBatteryInfoBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkModes() const
{
    return extractBits(kSinkModesBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkMinimumPdp() const
{
    return extractBits(kSinkMinimumPdpBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkOperationalPdp() const
{
    return extractBits(kSinkOperationalPdpBit, 8);
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkMaximumPdp() const
{
    return extractBits(kSinkMaximumPdpBit, 8);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751TransmitSinkCapsExtended::setSkedbVersion(uint8_t value)
{
    setBits(kSkedbVersionBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setLoadStep(uint8_t value)
{
    setBits(kLoadStepBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setSinkLoadCharacteristics(uint16_t value)
{
    setBits(kSinkLoadCharacteristicsBit, 16, value);
}

void TPS25751TransmitSinkCapsExtended::setCompliance(uint8_t value)
{
    setBits(kComplianceBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setTouchTemperature(uint8_t value)
{
    setBits(kTouchTemperatureBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setBatteryInfo(uint8_t value)
{
    setBits(kBatteryInfoBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setSinkModes(uint8_t value)
{
    setBits(kSinkModesBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setSinkMinimumPdp(uint8_t value)
{
    setBits(kSinkMinimumPdpBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setSinkOperationalPdp(uint8_t value)
{
    setBits(kSinkOperationalPdpBit, 8, value);
}

void TPS25751TransmitSinkCapsExtended::setSinkMaximumPdp(uint8_t value)
{
    setBits(kSinkMaximumPdpBit, 8, value);
}

// --- Debug ------------------------------------------------------------------------

void TPS25751TransmitSinkCapsExtended::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("TRANSMIT_SINK_CAPABILITIES_EXTENDED (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("Raw bytes (byte 0 first):"));
    for (size_t i = 0; i < size(); i++)
    {
        s.print(F(" "));
        printHex8(s, raw()[i]);
    }
    s.println();

    printByteField(s, F("SKEDB Version: "), skedbVersion());
    printByteField(s, F("Load Step: "), loadStep());
    s.print(F("Sink Load Characteristics: 0x"));
    printHex16(s, sinkLoadCharacteristics());
    s.println();
    printByteField(s, F("Compliance: "), compliance());
    printByteField(s, F("Touch Temperature: "), touchTemperature());
    printByteField(s, F("Battery Info: "), batteryInfo());
    printByteField(s, F("Sink Modes: "), sinkModes());
    printByteField(s, F("Sink Minimum PDP: "), sinkMinimumPdp());
    printByteField(s, F("Sink Operational PDP: "), sinkOperationalPdp());
    printByteField(s, F("Sink Maximum PDP: "), sinkMaximumPdp());

    const uint32_t reserved = extractBits32(kReservedBit, kReservedLen);
    if (reserved != 0)
    {
        s.print(F("Reserved bits set (111:88): 0x"));
        printHex8(s, static_cast<uint8_t>(reserved >> 16));
        printHex16(s, static_cast<uint16_t>(reserved & 0xFFFFu));
        s.println();
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751TransmitSinkCapsExtended::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES_EXTENDED.size;
    if (!isValidSize(expectedSize)) return false;

    // Reserved bits 111:88 are bytes 11, 12, and 13: a 24-bit field starting at
    // bit 88 (byte-aligned, so extractBits32 reads whole bytes).
    const uint32_t reserved = extractBits32(kReservedBit, kReservedLen);
    if (reserved != 0)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TRANSMIT_SINK_CAPABILITIES_EXTENDED: reserved bits 111:88 set (0x%06lX)", static_cast<unsigned long>(reserved));
        return false;
    }

    // No PDP ordering check: the TRM gives no encoding for the PDP bytes, so a
    // numeric comparison of minimum, operational, and maximum could reject a
    // valid value.
    return true;
}
