#include <Stream.h>

#include "TPS25751TransmitSourceCapsExtended.h"

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

uint8_t TPS25751TransmitSourceCapsExtended::voltageRegulation() const
{
    return extractBits(kVoltageRegulationBit, 8);
}

uint8_t TPS25751TransmitSourceCapsExtended::holdUpTime() const
{
    return extractBits(kHoldUpTimeBit, 8);
}

uint8_t TPS25751TransmitSourceCapsExtended::compliance() const
{
    return extractBits(kComplianceBit, 8);
}

uint8_t TPS25751TransmitSourceCapsExtended::touchCurrent() const
{
    return extractBits(kTouchCurrentBit, 8);
}

uint16_t TPS25751TransmitSourceCapsExtended::peakCurrent(uint8_t index) const
{
    if (index < 1 || index > kPeakCurrentCount) return 0;
    return extractBits16(static_cast<uint8_t>(kPeakCurrent1Bit + 16 * (index - 1)), 16);
}

uint8_t TPS25751TransmitSourceCapsExtended::touchTemperature() const
{
    return extractBits(kTouchTemperatureBit, 8);
}

uint8_t TPS25751TransmitSourceCapsExtended::sourceInputs() const
{
    return extractBits(kSourceInputsBit, 8);
}

uint8_t TPS25751TransmitSourceCapsExtended::numberFixedBatteries() const
{
    return extractBits(kNumberFixedBatteriesBit, 4);
}

uint8_t TPS25751TransmitSourceCapsExtended::numberHotSwappableBatteries() const
{
    return extractBits(kNumberHotSwappableBatteriesBit, 4);
}

uint8_t TPS25751TransmitSourceCapsExtended::sourcePdp() const
{
    return extractBits(kSourcePdpBit, 7);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751TransmitSourceCapsExtended::setVoltageRegulation(uint8_t value)
{
    setBits(kVoltageRegulationBit, 8, value);
}

void TPS25751TransmitSourceCapsExtended::setHoldUpTime(uint8_t value)
{
    setBits(kHoldUpTimeBit, 8, value);
}

void TPS25751TransmitSourceCapsExtended::setCompliance(uint8_t value)
{
    setBits(kComplianceBit, 8, value);
}

void TPS25751TransmitSourceCapsExtended::setTouchCurrent(uint8_t value)
{
    setBits(kTouchCurrentBit, 8, value);
}

void TPS25751TransmitSourceCapsExtended::setPeakCurrent(uint8_t index, uint16_t value)
{
    if (index < 1 || index > kPeakCurrentCount) return;
    setBits(static_cast<uint8_t>(kPeakCurrent1Bit + 16 * (index - 1)), 16, value);
}

void TPS25751TransmitSourceCapsExtended::setTouchTemperature(uint8_t value)
{
    setBits(kTouchTemperatureBit, 8, value);
}

void TPS25751TransmitSourceCapsExtended::setSourceInputs(uint8_t value)
{
    setBits(kSourceInputsBit, 8, value);
}

void TPS25751TransmitSourceCapsExtended::setNumberFixedBatteries(uint8_t value)
{
    setBits(kNumberFixedBatteriesBit, 4, value);
}

void TPS25751TransmitSourceCapsExtended::setNumberHotSwappableBatteries(uint8_t value)
{
    setBits(kNumberHotSwappableBatteriesBit, 4, value);
}

void TPS25751TransmitSourceCapsExtended::setSourcePdp(uint8_t value)
{
    setBits(kSourcePdpBit, 7, value);
}

// --- Debug ------------------------------------------------------------------------

void TPS25751TransmitSourceCapsExtended::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("TX_SOURCE_CAPABILITIES_EXTENDED (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED), HEX);
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

    printByteField(s, F("Voltage Regulation: "), voltageRegulation());
    printByteField(s, F("Hold Up Time: "), holdUpTime());
    printByteField(s, F("Compliance: "), compliance());
    printByteField(s, F("Touch Current: "), touchCurrent());
    for (uint8_t i = 1; i <= kPeakCurrentCount; i++)
    {
        s.print(F("Peak Current "));
        s.print(i);
        s.print(F(": 0x"));
        printHex16(s, peakCurrent(i));
        s.println();
    }
    printByteField(s, F("Touch Temperature: "), touchTemperature());
    printByteField(s, F("Source Inputs: "), sourceInputs());
    s.print(F("Number Fixed Batteries: "));
    s.println(numberFixedBatteries());
    s.print(F("Number Hot Swappable Batteries: "));
    s.println(numberHotSwappableBatteries());
    printByteField(s, F("Source PDP: "), sourcePdp());

    const uint16_t reserved = extractBits16(kReservedBit, kReservedLen);
    if (reserved != 0)
    {
        s.print(F("Reserved bits set (119:111): 0x"));
        printHex16(s, reserved);
        s.println();
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751TransmitSourceCapsExtended::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED.size;
    if (!isValidSize(expectedSize)) return false;

    // Reserved bits 119:111: bit 7 of byte 13 plus all 8 bits of byte 14, so a
    // 9-bit field starting at bit 111.
    const uint16_t reserved = extractBits16(kReservedBit, kReservedLen);
    if (reserved != 0)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TX_SOURCE_CAPABILITIES_EXTENDED: reserved bits 119:111 set (0x%03X)", static_cast<unsigned int>(reserved));
        return false;
    }

    if (numberFixedBatteries() > kMaxBatteryCount)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TX_SOURCE_CAPABILITIES_EXTENDED: Number Fixed Batteries %u exceeds %u", numberFixedBatteries(), kMaxBatteryCount);
        return false;
    }

    if (numberHotSwappableBatteries() > kMaxBatteryCount)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TX_SOURCE_CAPABILITIES_EXTENDED: Number Hot Swappable Batteries %u exceeds %u", numberHotSwappableBatteries(), kMaxBatteryCount);
        return false;
    }

    return true;
}
