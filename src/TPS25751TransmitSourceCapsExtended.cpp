#include <Stream.h>

#include "TPS25751TransmitSourceCapsExtended.h"

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

// --- Typed decode (USB PD R3.3 Table 6.59) -----------------------------------------

const char *TPS25751TransmitSourceCapsExtended::toString(LoadStep val)
{
    return TPS25751UsbPd::toString(val);
}

const char *TPS25751TransmitSourceCapsExtended::toString(TouchTemperature val)
{
    switch (val) {
        case TouchTemperature::Iec60950_1: return "IEC 60950-1";
        case TouchTemperature::Iec62368_1Ts1: return "IEC 62368-1 TS1";
        case TouchTemperature::Iec62368_1Ts2: return "IEC 62368-1 TS2";
        default: return "Invalid (assume IEC 60950-1)";
    }
}

TPS25751TransmitSourceCapsExtended::LoadStep TPS25751TransmitSourceCapsExtended::loadStep() const
{
    return TPS25751UsbPd::loadStepFromRaw(voltageRegulation());
}

uint8_t TPS25751TransmitSourceCapsExtended::iocPercent() const
{
    return (voltageRegulation() & 0x04u) ? 90 : 25;
}

bool TPS25751TransmitSourceCapsExtended::holdUpTimeSupported() const
{
    return holdUpTime() != 0;
}

uint8_t TPS25751TransmitSourceCapsExtended::holdUpTime_ms() const
{
    return holdUpTime();
}

bool TPS25751TransmitSourceCapsExtended::lpsCompliant() const
{
    return (compliance() & 0x01u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::ps1Compliant() const
{
    return (compliance() & 0x02u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::ps2Compliant() const
{
    return (compliance() & 0x04u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::lowTouchCurrentEps() const
{
    return (touchCurrent() & 0x01u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::groundPinSupported() const
{
    return (touchCurrent() & 0x02u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::groundPinProtectiveEarth() const
{
    return (touchCurrent() & 0x04u) != 0;
}

uint16_t TPS25751TransmitSourceCapsExtended::peakCurrentOverloadPercent(uint8_t index) const
{
    return TPS25751UsbPd::overloadPercent(peakCurrent(index));
}

uint16_t TPS25751TransmitSourceCapsExtended::peakCurrentOverloadPeriod_ms(uint8_t index) const
{
    return TPS25751UsbPd::overloadPeriod_ms(peakCurrent(index));
}

uint8_t TPS25751TransmitSourceCapsExtended::peakCurrentDutyCyclePercent(uint8_t index) const
{
    return TPS25751UsbPd::overloadDutyCyclePercent(peakCurrent(index));
}

bool TPS25751TransmitSourceCapsExtended::peakCurrentVbusDroop(uint8_t index) const
{
    return TPS25751UsbPd::overloadVbusDroop(peakCurrent(index));
}

TPS25751TransmitSourceCapsExtended::TouchTemperature TPS25751TransmitSourceCapsExtended::touchTemperatureStandard() const
{
    return static_cast<TouchTemperature>(touchTemperature());
}

bool TPS25751TransmitSourceCapsExtended::externalSupplyPresent() const
{
    return (sourceInputs() & 0x01u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::externalSupplyUnconstrained() const
{
    return (sourceInputs() & 0x02u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::internalBatteryPresent() const
{
    return (sourceInputs() & 0x04u) != 0;
}

bool TPS25751TransmitSourceCapsExtended::sourcePdpValid() const
{
    return sourcePdp() <= 100;
}

uint8_t TPS25751TransmitSourceCapsExtended::sourcePdp_W() const
{
    return sourcePdpValid() ? sourcePdp() : 0;
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
        printHex(s, raw()[i], 2);
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
        printHex(s, peakCurrent(i), 4);
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
        printHex(s, reserved, 4);
        s.println();
    }

    // Typed decode, USB PD R3.3 Table 6.59.
    s.print(F("Load Step: "));
    s.print(toString(loadStep()));
    s.print(F(", IoC: "));
    s.print(iocPercent());
    s.println(F("%"));
    s.print(F("Hold Up Time: "));
    if (holdUpTimeSupported())
    {
        s.print(holdUpTime_ms());
        s.println(F(" ms"));
    }
    else
    {
        s.println(F("not supported"));
    }
    s.print(F("Compliance: LPS: "));
    s.print(yesNo(lpsCompliant()));
    s.print(F(", PS1: "));
    s.print(yesNo(ps1Compliant()));
    s.print(F(", PS2: "));
    s.println(yesNo(ps2Compliant()));
    s.print(F("Touch Current: low touch current EPS: "));
    s.print(yesNo(lowTouchCurrentEps()));
    s.print(F(", ground pin: "));
    s.print(yesNo(groundPinSupported()));
    s.print(F(", protective earth: "));
    s.println(yesNo(groundPinProtectiveEarth()));
    for (uint8_t i = 1; i <= kPeakCurrentCount; i++)
    {
        s.print(F("Peak Current "));
        s.print(i);
        s.print(F(": "));
        s.print(peakCurrentOverloadPercent(i));
        s.print(F("% overload, "));
        s.print(peakCurrentOverloadPeriod_ms(i));
        s.print(F(" ms, "));
        s.print(peakCurrentDutyCyclePercent(i));
        s.print(F("% duty, VBUS droop: "));
        s.println(yesNo(peakCurrentVbusDroop(i)));
    }
    s.print(F("Touch Temperature standard: "));
    s.println(toString(touchTemperatureStandard()));
    s.print(F("Source Inputs: external supply: "));
    s.print(yesNo(externalSupplyPresent()));
    // Table 6.59: the receiver ignores bit 1 when bit 0 (external supply) is 0.
    if (externalSupplyPresent())
    {
        s.print(F(", unconstrained: "));
        s.print(yesNo(externalSupplyUnconstrained()));
    }
    s.print(F(", internal battery: "));
    s.println(yesNo(internalBatteryPresent()));
    s.print(F("SPR Source PDP: "));
    if (sourcePdpValid())
    {
        s.print(sourcePdp_W());
        s.println(F(" W"));
    }
    else
    {
        s.print(F("invalid ("));
        s.print(sourcePdp());
        s.println(F(")"));
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
