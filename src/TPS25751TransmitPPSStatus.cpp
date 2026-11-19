#include <Stream.h>

#include "TPS25751TransmitPPSStatus.h"

namespace {

void printHex32(Stream &s, uint32_t value)
{
    // Zero-padded to 8 hex digits to match the TRM's 00000000h notation;
    // Stream's HEX output drops leading zeros.
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        s.print(static_cast<unsigned int>((value >> shift) & 0xFu), HEX);
    }
}

}  // namespace

// --- String conversion ------------------------------------------------------------

const char *TPS25751TransmitPPSStatus::toString(PresentTemperatureFlag val)
{
    switch (val) {
        case PresentTemperatureFlag::NotDefined: return "Not defined (TRM)";
        case PresentTemperatureFlag::Normal: return "Normal";
        case PresentTemperatureFlag::Warning: return "Warning zone";
        case PresentTemperatureFlag::OverTemperature: return "Over-temperature";
        default: return "Undefined";
    }
}

const char *TPS25751TransmitPPSStatus::toString(OperatingMode val)
{
    switch (val) {
        case OperatingMode::ConstantVoltage: return "Constant Voltage (CV)";
        case OperatingMode::CurrentLimit: return "Current Limit (CL)";
        default: return "Undefined";
    }
}

// --- Decode ---------------------------------------------------------------------

uint16_t TPS25751TransmitPPSStatus::rawOutputVoltage() const
{
    return extractBits16(kOutputVoltageBit, 16);
}

bool TPS25751TransmitPPSStatus::outputVoltageSupported() const
{
    return rawOutputVoltage() != kVoltageNotSupported;
}

uint32_t TPS25751TransmitPPSStatus::outputVoltage_mV() const
{
    const uint16_t raw = rawOutputVoltage();
    if (raw == kVoltageNotSupported) return 0;
    return static_cast<uint32_t>(raw) * kVoltageLsb_mV;
}

uint8_t TPS25751TransmitPPSStatus::rawOutputCurrent() const
{
    return extractBits(kOutputCurrentBit, 8);
}

bool TPS25751TransmitPPSStatus::outputCurrentSupported() const
{
    return rawOutputCurrent() != kCurrentNotSupported;
}

uint16_t TPS25751TransmitPPSStatus::outputCurrent_mA() const
{
    const uint8_t raw = rawOutputCurrent();
    if (raw == kCurrentNotSupported) return 0;
    return static_cast<uint16_t>(raw * kCurrentLsb_mA);
}

TPS25751TransmitPPSStatus::PresentTemperatureFlag TPS25751TransmitPPSStatus::presentTemperatureFlag() const
{
    return static_cast<PresentTemperatureFlag>(rawPresentTemperatureFlag());
}

uint8_t TPS25751TransmitPPSStatus::rawPresentTemperatureFlag() const
{
    return extractBits(kPresentTemperatureFlagBit, 2);
}

TPS25751TransmitPPSStatus::OperatingMode TPS25751TransmitPPSStatus::operatingMode() const
{
    return static_cast<OperatingMode>(extractBits(kOperatingModeBit, 1));
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751TransmitPPSStatus::setRawOutputVoltage(uint16_t raw)
{
    setBits(kOutputVoltageBit, 16, raw);
}

void TPS25751TransmitPPSStatus::setRawOutputCurrent(uint8_t raw)
{
    setBits(kOutputCurrentBit, 8, raw);
}

void TPS25751TransmitPPSStatus::setOutputVoltage_mV(uint32_t mV)
{
    // Floor, then saturate one below the sentinel so an over-range request
    // cannot turn into "not supported".
    const uint32_t raw = mV / kVoltageLsb_mV;
    constexpr uint32_t maxRaw = kVoltageNotSupported - 1u;
    setRawOutputVoltage(static_cast<uint16_t>(raw > maxRaw ? maxRaw : raw));
}

void TPS25751TransmitPPSStatus::setOutputCurrent_mA(uint32_t mA)
{
    const uint32_t raw = mA / kCurrentLsb_mA;
    constexpr uint32_t maxRaw = kCurrentNotSupported - 1u;
    setRawOutputCurrent(static_cast<uint8_t>(raw > maxRaw ? maxRaw : raw));
}

void TPS25751TransmitPPSStatus::setOutputVoltageNotSupported()
{
    setRawOutputVoltage(kVoltageNotSupported);
}

void TPS25751TransmitPPSStatus::setOutputCurrentNotSupported()
{
    setRawOutputCurrent(kCurrentNotSupported);
}

void TPS25751TransmitPPSStatus::setPresentTemperatureFlag(PresentTemperatureFlag flag)
{
    setBits(kPresentTemperatureFlagBit, 2, static_cast<uint32_t>(flag));
}

void TPS25751TransmitPPSStatus::setOperatingMode(OperatingMode mode)
{
    setBits(kOperatingModeBit, 1, static_cast<uint32_t>(mode));
}

// --- Debug ------------------------------------------------------------------------

void TPS25751TransmitPPSStatus::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("TRANSMITTED_PPS_STATUS (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::TRANSMIT_PPS_STATUS), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    const uint32_t value = extractBits32(0, 32);
    s.print(F("Raw: 0x"));
    printHex32(s, value);
    s.println();

    s.print(F("Output Voltage: "));
    if (outputVoltageSupported())
    {
        s.print(rawOutputVoltage());
        s.print(F(" ("));
        s.print(static_cast<unsigned long>(outputVoltage_mV()));
        s.println(F(" mV)"));
    }
    else
    {
        s.print(rawOutputVoltage());
        s.println(F(" (not supported)"));
    }

    s.print(F("Output Current: "));
    if (outputCurrentSupported())
    {
        s.print(rawOutputCurrent());
        s.print(F(" ("));
        s.print(outputCurrent_mA());
        s.println(F(" mA)"));
    }
    else
    {
        s.print(rawOutputCurrent());
        s.println(F(" (not supported)"));
    }

    s.print(F("Present Temperature Flag: "));
    s.print(toString(presentTemperatureFlag()));
    s.print(F(" (raw "));
    s.print(rawPresentTemperatureFlag());
    s.println(F(")"));

    s.print(F("Operating Mode: "));
    s.println(toString(operatingMode()));

    const uint32_t reserved = value & kReservedMask;
    if (reserved != 0)
    {
        s.print(F("Reserved bits set: 0x"));
        printHex32(s, reserved);
        s.println();
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751TransmitPPSStatus::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_PPS_STATUS.size;
    if (!isValidSize(expectedSize)) return false;

    const uint32_t reserved = extractBits32(0, 32) & kReservedMask;
    if (reserved != 0)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TRANSMITTED_PPS_STATUS: reserved bits set (0x%08lX)", static_cast<unsigned long>(reserved));
        return false;
    }

    return true;
}
