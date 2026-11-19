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

const char *yesNo(bool value)
{
    return value ? "Yes" : "No";
}

// Prints "<label>valid W" or "<label>invalid (raw)".
void printPdp(Stream &s, const __FlashStringHelper *label, bool valid, uint8_t watts, uint8_t raw)
{
    s.print(label);
    if (valid)
    {
        s.print(watts);
        s.println(F(" W"));
    }
    else
    {
        s.print(F("invalid ("));
        s.print(raw);
        s.println(F(")"));
    }
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

// --- Typed decode (USB PD R3.3 Table 6.70) -----------------------------------------

const char *TPS25751TransmitSinkCapsExtended::toString(LoadStep val)
{
    switch (val) {
        case LoadStep::Rate150mAPerUs: return "150 mA/us";
        case LoadStep::Rate500mAPerUs: return "500 mA/us";
        default: return "Invalid (assume 150 mA/us)";
    }
}

const char *TPS25751TransmitSinkCapsExtended::toString(TouchTemperature val)
{
    switch (val) {
        case TouchTemperature::NoApplicableStandard: return "No applicable standard";
        case TouchTemperature::Iec60950_1: return "IEC 60950-1";
        case TouchTemperature::Iec62368_1Ts1: return "IEC 62368-1 TS1";
        case TouchTemperature::Iec62368_1Ts2: return "IEC 62368-1 TS2";
        default: return "Invalid (assume IEC 60950-1)";
    }
}

bool TPS25751TransmitSinkCapsExtended::skedbVersionValid() const
{
    return skedbVersion() == 1;
}

TPS25751TransmitSinkCapsExtended::LoadStep TPS25751TransmitSinkCapsExtended::loadStepRate() const
{
    return static_cast<LoadStep>(loadStep() & 0x03u);
}

uint16_t TPS25751TransmitSinkCapsExtended::sinkLoadOverloadPercent() const
{
    uint16_t steps = sinkLoadCharacteristics() & 0x1Fu;  // bits 4:0
    if (steps > 25) steps = 25;                          // 26..31 invalid, treated as 25
    return static_cast<uint16_t>(steps * 10);
}

uint16_t TPS25751TransmitSinkCapsExtended::sinkLoadOverloadPeriod_ms() const
{
    return static_cast<uint16_t>(((sinkLoadCharacteristics() >> 5) & 0x3Fu) * 20);  // bits 10:5, 20 ms steps
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkLoadDutyCyclePercent() const
{
    return static_cast<uint8_t>(((sinkLoadCharacteristics() >> 11) & 0x0Fu) * 5);  // bits 14:11, 5 % steps
}

bool TPS25751TransmitSinkCapsExtended::sinkLoadVbusDroopTolerated() const
{
    return (sinkLoadCharacteristics() & 0x8000u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::lpsCompliant() const
{
    return (compliance() & 0x01u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::ps1Compliant() const
{
    return (compliance() & 0x02u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::ps2Compliant() const
{
    return (compliance() & 0x04u) != 0;
}

TPS25751TransmitSinkCapsExtended::TouchTemperature TPS25751TransmitSinkCapsExtended::touchTemperatureStandard() const
{
    return static_cast<TouchTemperature>(touchTemperature());
}

uint8_t TPS25751TransmitSinkCapsExtended::fixedBatteryCount() const
{
    return batteryInfo() & 0x0Fu;
}

uint8_t TPS25751TransmitSinkCapsExtended::hotSwappableBatterySlotCount() const
{
    return static_cast<uint8_t>((batteryInfo() >> 4) & 0x0Fu);
}

bool TPS25751TransmitSinkCapsExtended::ppsChargingSupported() const
{
    return (sinkModes() & 0x01u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::vbusPowered() const
{
    return (sinkModes() & 0x02u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::acSupplyPowered() const
{
    return (sinkModes() & 0x04u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::batteryPowered() const
{
    return (sinkModes() & 0x08u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::batteryEssentiallyUnlimited() const
{
    return (sinkModes() & 0x10u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::avsSupported() const
{
    return (sinkModes() & 0x20u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::vconnPoweredAccessory() const
{
    return (sinkModes() & 0x40u) != 0;
}

bool TPS25751TransmitSinkCapsExtended::sinkMinimumPdpValid() const
{
    return sinkMinimumPdp() <= 100;
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkMinimumPdp_W() const
{
    return sinkMinimumPdpValid() ? sinkMinimumPdp() : 0;
}

bool TPS25751TransmitSinkCapsExtended::sinkOperationalPdpValid() const
{
    return sinkOperationalPdp() <= 100;
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkOperationalPdp_W() const
{
    return sinkOperationalPdpValid() ? sinkOperationalPdp() : 0;
}

bool TPS25751TransmitSinkCapsExtended::sinkMaximumPdpValid() const
{
    return sinkMaximumPdp() <= 100;
}

uint8_t TPS25751TransmitSinkCapsExtended::sinkMaximumPdp_W() const
{
    return sinkMaximumPdpValid() ? sinkMaximumPdp() : 0;
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

    // Typed decode, USB PD R3.3 Table 6.70.
    s.print(F("SKEDB Version valid: "));
    s.println(yesNo(skedbVersionValid()));
    s.print(F("Load Step rate: "));
    s.println(toString(loadStepRate()));
    s.print(F("Sink Load: "));
    s.print(sinkLoadOverloadPercent());
    s.print(F("% overload, "));
    s.print(sinkLoadOverloadPeriod_ms());
    s.print(F(" ms, "));
    s.print(sinkLoadDutyCyclePercent());
    s.print(F("% duty, VBUS droop tolerated: "));
    s.println(yesNo(sinkLoadVbusDroopTolerated()));
    s.print(F("Compliance: LPS: "));
    s.print(yesNo(lpsCompliant()));
    s.print(F(", PS1: "));
    s.print(yesNo(ps1Compliant()));
    s.print(F(", PS2: "));
    s.println(yesNo(ps2Compliant()));
    s.print(F("Touch Temperature standard: "));
    s.println(toString(touchTemperatureStandard()));
    s.print(F("Batteries: "));
    s.print(fixedBatteryCount());
    s.print(F(" fixed, "));
    s.print(hotSwappableBatterySlotCount());
    s.println(F(" hot swappable slots"));
    s.print(F("Sink Modes: PPS charging: "));
    s.print(yesNo(ppsChargingSupported()));
    s.print(F(", VBUS powered: "));
    s.print(yesNo(vbusPowered()));
    s.print(F(", AC powered: "));
    s.print(yesNo(acSupplyPowered()));
    s.print(F(", battery powered: "));
    s.print(yesNo(batteryPowered()));
    s.print(F(", battery unlimited: "));
    s.print(yesNo(batteryEssentiallyUnlimited()));
    s.print(F(", AVS: "));
    s.print(yesNo(avsSupported()));
    s.print(F(", VCONN accessory: "));
    s.println(yesNo(vconnPoweredAccessory()));
    printPdp(s, F("SPR Sink Minimum PDP: "), sinkMinimumPdpValid(), sinkMinimumPdp_W(), sinkMinimumPdp());
    printPdp(s, F("SPR Sink Operational PDP: "), sinkOperationalPdpValid(), sinkOperationalPdp_W(), sinkOperationalPdp());
    printPdp(s, F("SPR Sink Maximum PDP: "), sinkMaximumPdpValid(), sinkMaximumPdp_W(), sinkMaximumPdp());

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
