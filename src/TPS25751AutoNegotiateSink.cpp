#include "TPS25751AutoNegotiateSink.h"

// --- Decode: 1-bit fields ----------------------------------------------------

bool TPS25751AutoNegotiateSink::rdoPriorityPrefersLowerVoltage() const
{
    return extractBits(kRdoPriorityBit, 1);
}

bool TPS25751AutoNegotiateSink::noUSBSuspend() const
{
    return extractBits(kNoUSBSuspendBit, 1);
}

bool TPS25751AutoNegotiateSink::autoComputeSinkMinPower() const
{
    return extractBits(kAutoComputeSinkMinPowerBit, 1);
}

bool TPS25751AutoNegotiateSink::noCapabilityMismatch() const
{
    return extractBits(kNoCapabilityMismatchBit, 1);
}

bool TPS25751AutoNegotiateSink::autoComputeSinkMinVoltage() const
{
    return extractBits(kAutoComputeSinkMinVoltageBit, 1);
}

bool TPS25751AutoNegotiateSink::autoComputeSinkMaxVoltage() const
{
    return extractBits(kAutoComputeSinkMaxVoltageBit, 1);
}

bool TPS25751AutoNegotiateSink::autoDisableSinkUponCapabilityMismatch() const
{
    return extractBits(kAutoDisableSinkUponCapMismatchBit, 1);
}

// --- Decode: scaled multi-bit fields -----------------------------------------

uint16_t TPS25751AutoNegotiateSink::rawAutoNegMaxCurrent() const
{
    return extractBits16(kAutoNegMaxCurrentShift, kAutoNegMaxCurrentBits);
}

uint16_t TPS25751AutoNegotiateSink::autoNegMaxCurrent_mA() const
{
    return static_cast<uint16_t>(rawAutoNegMaxCurrent() * kMilliampsPerLsb10);
}

uint16_t TPS25751AutoNegotiateSink::rawSinkMinRequiredPower() const
{
    return extractBits16(kSinkMinRequiredPowerShift, kSinkMinRequiredPowerBits);
}

uint32_t TPS25751AutoNegotiateSink::sinkMinRequiredPower_mW() const
{
    return static_cast<uint32_t>(rawSinkMinRequiredPower()) * kMilliwattsPerLsb250;
}

uint16_t TPS25751AutoNegotiateSink::rawAutoNegMaxVoltage() const
{
    return extractBits16(kAutoNegMaxVoltageShift, kAutoNegMaxVoltageBits);
}

uint16_t TPS25751AutoNegotiateSink::autoNegMaxVoltage_mV() const
{
    return static_cast<uint16_t>(rawAutoNegMaxVoltage()) * kMillivoltsPerLsb50;
}

uint16_t TPS25751AutoNegotiateSink::rawAutoNegMinVoltage() const
{
    return extractBits16(kAutoNegMinVoltageShift, kAutoNegMinVoltageBits);
}

uint16_t TPS25751AutoNegotiateSink::autoNegMinVoltage_mV() const
{
    return static_cast<uint16_t>(rawAutoNegMinVoltage()) * kMillivoltsPerLsb50;
}

uint16_t TPS25751AutoNegotiateSink::rawCapMismatchPower() const
{
    return extractBits16(kCapMismatchPowerShift, kCapMismatchPowerBits);
}

uint32_t TPS25751AutoNegotiateSink::capMismatchPower_mW() const
{
    return static_cast<uint32_t>(rawCapMismatchPower()) * kMilliwattsPerLsb250;
}

// --- Decode: PPS sink fields ---------------------------------------------------

bool TPS25751AutoNegotiateSink::ppsEnableSinkMode() const
{
    return extractBits(kPpsEnableSinkModeBit, 1);
}

PpsRequestInterval TPS25751AutoNegotiateSink::ppsRequestInterval() const
{
    return extractEnum<PpsRequestInterval>(kPpsRequestIntervalShift, kPpsRequestIntervalBits,
                                            static_cast<uint8_t>(PpsRequestInterval::Interval1s),
                                            PpsRequestInterval::Interval8s);
}

bool TPS25751AutoNegotiateSink::ppsOperatingModeConstantVoltage() const
{
    return extractBits(kPpsOperatingModeBit, 1);
}

bool TPS25751AutoNegotiateSink::ppsRequiredFullVoltageRange() const
{
    return extractBits(kPpsRequiredFullVoltageRangeBit, 1);
}

bool TPS25751AutoNegotiateSink::ppsDisableSinkUponNonApdoContract() const
{
    return extractBits(kPpsDisableSinkUponNonApdoBit, 1);
}

uint8_t TPS25751AutoNegotiateSink::rawPpsOperatingCurrent() const
{
    return extractBits(kPpsOperatingCurrentShift, kPpsOperatingCurrentBits);
}

uint16_t TPS25751AutoNegotiateSink::ppsOperatingCurrent_mA() const
{
    return static_cast<uint16_t>(rawPpsOperatingCurrent()) * kMilliampsPerLsb50;
}

uint16_t TPS25751AutoNegotiateSink::rawPpsOperatingVoltage() const
{
    return extractBits16(kPpsOperatingVoltageShift, kPpsOperatingVoltageBits);
}

uint16_t TPS25751AutoNegotiateSink::ppsOperatingVoltage_mV() const
{
    return static_cast<uint16_t>(rawPpsOperatingVoltage()) * kMillivoltsPerLsb20;
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751AutoNegotiateSink::setRdoPriorityPrefersLowerVoltage(bool preferLowerVoltage)
{
    setBits(kRdoPriorityBit, 1, preferLowerVoltage ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setNoUSBSuspend(bool value)
{
    setBits(kNoUSBSuspendBit, 1, value ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setAutoComputeSinkMinPower(bool computedByController)
{
    setBits(kAutoComputeSinkMinPowerBit, 1, computedByController ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setNoCapabilityMismatch(bool disabled)
{
    setBits(kNoCapabilityMismatchBit, 1, disabled ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setAutoComputeSinkMinVoltage(bool computedByController)
{
    setBits(kAutoComputeSinkMinVoltageBit, 1, computedByController ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setAutoComputeSinkMaxVoltage(bool computedByController)
{
    setBits(kAutoComputeSinkMaxVoltageBit, 1, computedByController ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setAutoDisableSinkUponCapabilityMismatch(bool enabled)
{
    setBits(kAutoDisableSinkUponCapMismatchBit, 1, enabled ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setRawAutoNegMaxCurrent(uint16_t raw)
{
    setBits(kAutoNegMaxCurrentShift, kAutoNegMaxCurrentBits, raw);
}

void TPS25751AutoNegotiateSink::setRawSinkMinRequiredPower(uint16_t raw)
{
    setBits(kSinkMinRequiredPowerShift, kSinkMinRequiredPowerBits, raw);
}

void TPS25751AutoNegotiateSink::setRawAutoNegMaxVoltage(uint16_t raw)
{
    setBits(kAutoNegMaxVoltageShift, kAutoNegMaxVoltageBits, raw);
}

void TPS25751AutoNegotiateSink::setRawAutoNegMinVoltage(uint16_t raw)
{
    setBits(kAutoNegMinVoltageShift, kAutoNegMinVoltageBits, raw);
}

void TPS25751AutoNegotiateSink::setRawCapMismatchPower(uint16_t raw)
{
    setBits(kCapMismatchPowerShift, kCapMismatchPowerBits, raw);
}

void TPS25751AutoNegotiateSink::setPpsEnableSinkMode(bool enabled)
{
    setBits(kPpsEnableSinkModeBit, 1, enabled ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setPpsRequestInterval(PpsRequestInterval interval)
{
    setBits(kPpsRequestIntervalShift, kPpsRequestIntervalBits, static_cast<uint8_t>(interval));
}

void TPS25751AutoNegotiateSink::setPpsOperatingModeConstantVoltage(bool constantVoltage)
{
    setBits(kPpsOperatingModeBit, 1, constantVoltage ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setPpsRequiredFullVoltageRange(bool required)
{
    setBits(kPpsRequiredFullVoltageRangeBit, 1, required ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setPpsDisableSinkUponNonApdoContract(bool disable)
{
    setBits(kPpsDisableSinkUponNonApdoBit, 1, disable ? 1u : 0u);
}

void TPS25751AutoNegotiateSink::setRawPpsOperatingCurrent(uint8_t raw)
{
    setBits(kPpsOperatingCurrentShift, kPpsOperatingCurrentBits, raw);
}

void TPS25751AutoNegotiateSink::setRawPpsOperatingVoltage(uint16_t raw)
{
    setBits(kPpsOperatingVoltageShift, kPpsOperatingVoltageBits, raw);
}

void TPS25751AutoNegotiateSink::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("AUTONEGOTIATE_SINK (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::AUTONEGOTIATE_SINK), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("RDO Priority: "));
    s.println(rdoPriorityPrefersLowerVoltage() ? F("Lower Voltage") : F("Higher Voltage"));

    s.print(F("No USB Suspend: "));
    s.println(noUSBSuspend() ? F("Yes") : F("No"));

    s.print(F("Auto Compute Sink Min Power: "));
    s.println(autoComputeSinkMinPower() ? F("Computed by PD controller") : F("Provided by host"));

    s.print(F("No Capability Mismatch: "));
    s.println(noCapabilityMismatch() ? F("Disabled") : F("Enabled"));

    s.print(F("Auto Compute Sink Min Voltage: "));
    s.println(autoComputeSinkMinVoltage() ? F("Computed by PD controller") : F("Provided by host"));

    s.print(F("Auto Compute Sink Max Voltage: "));
    s.println(autoComputeSinkMaxVoltage() ? F("Computed by PD controller") : F("Provided by host"));

    s.print(F("Auto Disable Sink Upon Capability Mismatch: "));
    s.println(autoDisableSinkUponCapabilityMismatch() ? F("Yes") : F("No"));

    s.print(F("Auto Neg Max Current: "));
    s.print(autoNegMaxCurrent_mA());
    s.print(F(" mA (raw "));
    s.print(rawAutoNegMaxCurrent());
    s.println(F(")"));

    s.print(F("Sink Min Required Power: "));
    s.print(sinkMinRequiredPower_mW());
    s.print(F(" mW (raw "));
    s.print(rawSinkMinRequiredPower());
    s.println(F(")"));

    s.print(F("Auto Neg Max Voltage: "));
    s.print(autoNegMaxVoltage_mV());
    s.print(F(" mV (raw "));
    s.print(rawAutoNegMaxVoltage());
    s.println(F(")"));

    s.print(F("Auto Neg Min Voltage: "));
    s.print(autoNegMinVoltage_mV());
    s.print(F(" mV (raw "));
    s.print(rawAutoNegMinVoltage());
    s.println(F(")"));

    s.print(F("Capabilities Mismatch Power: "));
    s.print(capMismatchPower_mW());
    s.print(F(" mW (raw "));
    s.print(rawCapMismatchPower());
    s.println(F(")"));

    s.print(F("PPS Enable Sink Mode: "));
    s.println(ppsEnableSinkMode() ? F("Yes") : F("No"));

    s.print(F("PPS Request Interval: "));
    s.println(toString(ppsRequestInterval()));

    s.print(F("PPS Operating Mode: "));
    s.println(ppsOperatingModeConstantVoltage() ? F("Constant Voltage (CV)") : F("Current Limit (CC)"));

    s.print(F("PPS Required Full Voltage Range: "));
    s.println(ppsRequiredFullVoltageRange() ? F("Yes") : F("No"));

    s.print(F("PPS Disable Sink Upon Non-APDO Contract: "));
    s.println(ppsDisableSinkUponNonApdoContract() ? F("Yes") : F("No"));

    s.print(F("PPS Operating Current: "));
    s.print(ppsOperatingCurrent_mA());
    s.print(F(" mA (raw "));
    s.print(rawPpsOperatingCurrent());
    s.println(F(")"));

    s.print(F("PPS Operating Voltage: "));
    s.print(ppsOperatingVoltage_mV());
    s.print(F(" mV (raw "));
    s.print(rawPpsOperatingVoltage());
    s.println(F(")"));

    s.println(F("========================================"));
}

bool TPS25751AutoNegotiateSink::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::AUTONEGOTIATE_SINK.size;
    if (!isValidSize(expectedSize)) return false;

    // Cross-field rule: when both bounds are host-provided (not PD-computed),
    // the host-provided minimum must not exceed the host-provided maximum.
    if (!autoComputeSinkMinVoltage() && !autoComputeSinkMaxVoltage()) {
        if (rawAutoNegMinVoltage() > rawAutoNegMaxVoltage()) {
            return false;
        }
    }

    return true;
}

const char* toString(PpsRequestInterval val)
{
    switch (val) {
        case PpsRequestInterval::Interval8s: return "8 seconds";
        case PpsRequestInterval::Interval4s: return "4 seconds";
        case PpsRequestInterval::Interval2s: return "2 seconds";
        case PpsRequestInterval::Interval1s: return "1 second";
        default: return "Unknown";
    }
}
