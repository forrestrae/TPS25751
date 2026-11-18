#include "TPS25751ADCResults.h"

uint8_t TPS25751ADCResults::rawADCIN1() const
{
    return extractBits(kADCIN1BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::adcin1Millivolts() const
{
    return static_cast<uint16_t>(rawADCIN1()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawADCIN2() const
{
    return extractBits(kADCIN2BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::adcin2Millivolts() const
{
    return static_cast<uint16_t>(rawADCIN2()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawLDO3V3() const
{
    return extractBits(kLDO3V3BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::ldo3v3Millivolts() const
{
    return static_cast<uint16_t>(rawLDO3V3()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawVBUS() const
{
    return extractBits(kVBUSBitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::vbusMillivolts() const
{
    return static_cast<uint16_t>(rawVBUS()) * kVbusMillivoltsPerLsb;
}

uint8_t TPS25751ADCResults::rawIVBUSMean() const
{
    return extractBits(kIVBUSMeanBitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::ivbusMeanMilliamps() const
{
    return static_cast<uint16_t>(rawIVBUSMean()) * kMilliampsPerLsbNumerator / kMilliampsPerLsbDenominator;
}

uint8_t TPS25751ADCResults::rawGPIO4() const
{
    return extractBits(kGPIO4BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::gpio4Millivolts() const
{
    return static_cast<uint16_t>(rawGPIO4()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawGPIO5() const
{
    return extractBits(kGPIO5BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::gpio5Millivolts() const
{
    return static_cast<uint16_t>(rawGPIO5()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawGPIO0() const
{
    return extractBits(kGPIO0BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::gpio0Millivolts() const
{
    return static_cast<uint16_t>(rawGPIO0()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawGPIO2() const
{
    return extractBits(kGPIO2BitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::gpio2Millivolts() const
{
    return static_cast<uint16_t>(rawGPIO2()) * kMillivoltsPerLsb14;
}

uint8_t TPS25751ADCResults::rawIVBUSPeak() const
{
    return extractBits(kIVBUSPeakBitPos, kFieldBitLen);
}

uint16_t TPS25751ADCResults::ivbusPeakMilliamps() const
{
    return static_cast<uint16_t>(rawIVBUSPeak()) * kMilliampsPerLsbNumerator / kMilliampsPerLsbDenominator;
}

void TPS25751ADCResults::debugPrint(Stream &s) const
{
    s.println(F("=== ADC Results Register (0x6A) ==="));

    s.print(F("  ADCIN1: "));
    s.print(adcin1Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawADCIN1());
    s.println(F(")"));

    s.print(F("  ADCIN2: "));
    s.print(adcin2Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawADCIN2());
    s.println(F(")"));

    s.print(F("  LDO3V3: "));
    s.print(ldo3v3Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawLDO3V3());
    s.println(F(")"));

    s.print(F("  VBUS: "));
    s.print(vbusMillivolts());
    s.print(F(" mV (raw "));
    s.print(rawVBUS());
    s.println(F(")"));

    s.print(F("  IVBUS_Mean: "));
    s.print(ivbusMeanMilliamps());
    s.print(F(" mA (raw "));
    s.print(rawIVBUSMean());
    s.println(F(")"));

    s.print(F("  GPIO4: "));
    s.print(gpio4Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawGPIO4());
    s.println(F(")"));

    s.print(F("  GPIO5: "));
    s.print(gpio5Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawGPIO5());
    s.println(F(")"));

    s.print(F("  GPIO0: "));
    s.print(gpio0Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawGPIO0());
    s.println(F(")"));

    s.print(F("  GPIO2: "));
    s.print(gpio2Millivolts());
    s.print(F(" mV (raw "));
    s.print(rawGPIO2());
    s.println(F(")"));

    s.print(F("  IVBUS_Peak: "));
    s.print(ivbusPeakMilliamps());
    s.print(F(" mA (raw "));
    s.print(rawIVBUSPeak());
    s.println(F(")"));

    s.println(F("  NOTE: IVBUS_Mean/IVBUS_Peak need PORT_CONTROL.EnableCurrentMonitor = 1."));
    s.println(F("  NOTE: not updated by the PD controller in Unconnected Sleep mode."));

    s.println();
}

bool TPS25751ADCResults::isSemanticallyValid() const
{
    // Basic validity checks first. hasValidData() only flags the all-0xFF
    // bus-error signature -- an all-zero buffer (the legitimate reset /
    // never-measured state, e.g. Unconnected Sleep) passes it.
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::ADC_RESULTS.size;
    if (!isValidSize(expectedSize)) return false;

    // Every field is an independent ADC channel sample; there are no
    // cross-field consistency rules to enforce.

    return true;
}
