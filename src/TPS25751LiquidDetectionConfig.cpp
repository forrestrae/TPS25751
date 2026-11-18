#include "TPS25751LiquidDetectionConfig.h"

// --- Decode: wait-interval fields --------------------------------------------

uint16_t TPS25751LiquidDetectionConfig::rawWaitTimeNoLiquid() const
{
    return extractBits16(kWaitTimeNoLiquidShift, kWaitTimeNoLiquidBits);
}

uint32_t TPS25751LiquidDetectionConfig::waitTimeNoLiquid_ms() const
{
    return static_cast<uint32_t>(rawWaitTimeNoLiquid()) * kMillisecondsPerLsb1000;
}

uint16_t TPS25751LiquidDetectionConfig::rawWaitTimeLiquid() const
{
    return extractBits16(kWaitTimeLiquidShift, kWaitTimeLiquidBits);
}

uint32_t TPS25751LiquidDetectionConfig::waitTimeLiquid_ms() const
{
    return static_cast<uint32_t>(rawWaitTimeLiquid()) * kMillisecondsPerLsb1000;
}

// --- Decode: sampling configuration -------------------------------------------

uint8_t TPS25751LiquidDetectionConfig::numberOfSamples() const
{
    return extractBits(kNumberOfSamplesShift, kNumberOfSamplesBits);
}

// --- Decode: ADC threshold fields ---------------------------------------------

uint8_t TPS25751LiquidDetectionConfig::rawLowThresholdNoLiquid() const
{
    return extractBits(kLowThresholdNoLiquidShift, kLowThresholdNoLiquidBits);
}

uint16_t TPS25751LiquidDetectionConfig::lowThresholdNoLiquid_mV() const
{
    return static_cast<uint16_t>(rawLowThresholdNoLiquid()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751LiquidDetectionConfig::rawHighThresholdNoLiquid() const
{
    return extractBits(kHighThresholdNoLiquidShift, kHighThresholdNoLiquidBits);
}

uint16_t TPS25751LiquidDetectionConfig::highThresholdNoLiquid_mV() const
{
    return static_cast<uint16_t>(rawHighThresholdNoLiquid()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751LiquidDetectionConfig::rawLowThresholdLiquid() const
{
    return extractBits(kLowThresholdLiquidShift, kLowThresholdLiquidBits);
}

uint16_t TPS25751LiquidDetectionConfig::lowThresholdLiquid_mV() const
{
    return static_cast<uint16_t>(rawLowThresholdLiquid()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751LiquidDetectionConfig::rawHighThresholdLiquid() const
{
    return extractBits(kHighThresholdLiquidShift, kHighThresholdLiquidBits);
}

uint16_t TPS25751LiquidDetectionConfig::highThresholdLiquid_mV() const
{
    return static_cast<uint16_t>(rawHighThresholdLiquid()) * kMillivoltsPerLsb14;
}

// --- Decode: ADC sample-time fields -------------------------------------------

uint8_t TPS25751LiquidDetectionConfig::rawSampleTimeNoLiquid() const
{
    return extractBits(kSampleTimeNoLiquidShift, kSampleTimeNoLiquidBits);
}

uint16_t TPS25751LiquidDetectionConfig::sampleTimeNoLiquid_ms() const
{
    return static_cast<uint16_t>(rawSampleTimeNoLiquid()) * kMillisecondsPerLsb10;
}

uint8_t TPS25751LiquidDetectionConfig::rawSampleTimeLiquid() const
{
    return extractBits(kSampleTimeLiquidShift, kSampleTimeLiquidBits);
}

uint16_t TPS25751LiquidDetectionConfig::sampleTimeLiquid_ms() const
{
    return static_cast<uint16_t>(rawSampleTimeLiquid()) * kMillisecondsPerLsb10;
}

// --- Decode: state / enable bits ----------------------------------------------

bool TPS25751LiquidDetectionConfig::liquidDetectionState() const
{
    return extractBits(kLiquidDetectionStateBit, 1);
}

bool TPS25751LiquidDetectionConfig::enableCorrosionMitigation() const
{
    return extractBits(kEnableCorrosionMitigationBit, 1);
}

bool TPS25751LiquidDetectionConfig::enableLiquidDetection() const
{
    return extractBits(kEnableLiquidDetectionBit, 1);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751LiquidDetectionConfig::setRawWaitTimeNoLiquid(uint16_t raw)
{
    setBits(kWaitTimeNoLiquidShift, kWaitTimeNoLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawWaitTimeLiquid(uint16_t raw)
{
    setBits(kWaitTimeLiquidShift, kWaitTimeLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setNumberOfSamples(uint8_t raw)
{
    setBits(kNumberOfSamplesShift, kNumberOfSamplesBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawLowThresholdNoLiquid(uint8_t raw)
{
    setBits(kLowThresholdNoLiquidShift, kLowThresholdNoLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawHighThresholdNoLiquid(uint8_t raw)
{
    setBits(kHighThresholdNoLiquidShift, kHighThresholdNoLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawLowThresholdLiquid(uint8_t raw)
{
    setBits(kLowThresholdLiquidShift, kLowThresholdLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawHighThresholdLiquid(uint8_t raw)
{
    setBits(kHighThresholdLiquidShift, kHighThresholdLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawSampleTimeNoLiquid(uint8_t raw)
{
    setBits(kSampleTimeNoLiquidShift, kSampleTimeNoLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setRawSampleTimeLiquid(uint8_t raw)
{
    setBits(kSampleTimeLiquidShift, kSampleTimeLiquidBits, raw);
}

void TPS25751LiquidDetectionConfig::setLiquidDetectionState(bool detected)
{
    setBits(kLiquidDetectionStateBit, 1, detected ? 1u : 0u);
}

void TPS25751LiquidDetectionConfig::setEnableCorrosionMitigation(bool enabled)
{
    setBits(kEnableCorrosionMitigationBit, 1, enabled ? 1u : 0u);
}

void TPS25751LiquidDetectionConfig::setEnableLiquidDetection(bool enabled)
{
    setBits(kEnableLiquidDetectionBit, 1, enabled ? 1u : 0u);
}

void TPS25751LiquidDetectionConfig::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("LIQUID_DETECTION_CONFIG (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::LIQUID_DETECTION_CONFIG), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("Wait Time No Liquid: "));
    s.print(waitTimeNoLiquid_ms());
    s.print(F(" ms (raw "));
    s.print(rawWaitTimeNoLiquid());
    s.println(F(")"));

    s.print(F("Wait Time Liquid: "));
    s.print(waitTimeLiquid_ms());
    s.print(F(" ms (raw "));
    s.print(rawWaitTimeLiquid());
    s.println(F(")"));

    s.print(F("Number of Samples: "));
    s.println(numberOfSamples());

    s.print(F("Low Threshold No Liquid: "));
    s.print(lowThresholdNoLiquid_mV());
    s.print(F(" mV (raw "));
    s.print(rawLowThresholdNoLiquid());
    s.println(F(")"));

    s.print(F("High Threshold No Liquid: "));
    s.print(highThresholdNoLiquid_mV());
    s.print(F(" mV (raw "));
    s.print(rawHighThresholdNoLiquid());
    s.println(F(")"));

    s.print(F("Low Threshold Liquid: "));
    s.print(lowThresholdLiquid_mV());
    s.print(F(" mV (raw "));
    s.print(rawLowThresholdLiquid());
    s.println(F(")"));

    s.print(F("High Threshold Liquid: "));
    s.print(highThresholdLiquid_mV());
    s.print(F(" mV (raw "));
    s.print(rawHighThresholdLiquid());
    s.println(F(")"));

    s.print(F("Sample Time No Liquid: "));
    s.print(sampleTimeNoLiquid_ms());
    s.print(F(" ms (raw "));
    s.print(rawSampleTimeNoLiquid());
    s.println(F(")"));

    s.print(F("Sample Time Liquid: "));
    s.print(sampleTimeLiquid_ms());
    s.print(F(" ms (raw "));
    s.print(rawSampleTimeLiquid());
    s.println(F(")"));

    s.print(F("Liquid Detection State: "));
    s.println(liquidDetectionState() ? F("Liquid Detected") : F("No Liquid"));

    s.print(F("Enable Corrosion Mitigation: "));
    s.println(enableCorrosionMitigation() ? F("Enabled") : F("Disabled"));

    s.print(F("Enable Liquid Detection: "));
    s.println(enableLiquidDetection() ? F("Enabled") : F("Disabled"));

    s.println(F("========================================"));
}

bool TPS25751LiquidDetectionConfig::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::LIQUID_DETECTION_CONFIG.size;
    if (!isValidSize(expectedSize)) return false;

    // Cross-field rule: each threshold pair's Low bound must not exceed its
    // High bound (hysteresis window for exit out of Liquid Detected).
    if (rawLowThresholdNoLiquid() > rawHighThresholdNoLiquid()) {
        return false;
    }
    if (rawLowThresholdLiquid() > rawHighThresholdLiquid()) {
        return false;
    }

    return true;
}
