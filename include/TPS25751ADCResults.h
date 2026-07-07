#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 ADC Results Register (ADC_RESULTS, Address: 0x6A)
 *
 * Provides access to measurements from the internal ADC. The PD controller
 * periodically measures the pins listed below and stores the most recent
 * 8-bit reading for each in this register. The frequency of the update
 * depends upon the mode of the PD controller; notably, **in Unconnected
 * Sleep the PD controller does not update these registers**, so a stale or
 * all-zero read while asleep is expected and is not, by itself, an error.
 *
 * Every field in this register is exactly one byte (8 bits) wide. Byte 0 is
 * the least-significant byte of the register per the library's
 * little-endian buffer assembly convention.
 *
 * Register Details:
 * - Address: 0x6A
 * - Size: 13 bytes
 * - Access: Read-only
 *
 * Byte layout (not alphabetical -- transcribed directly from the TRM):
 * | Byte | Field        | Unit / LSB    |
 * |------|--------------|---------------|
 * | 0    | ADCIN1       | 14 mV/LSB     |
 * | 1    | ADCIN2       | 14 mV/LSB     |
 * | 2    | LDO3V3       | 14 mV/LSB     |
 * | 3    | VBUS1        | 98 mV/LSB     |
 * | 4    | Reserved     | --            |
 * | 5    | IVBUS1       | 16.5 mA/LSB   |
 * | 6    | Reserved     | --            |
 * | 7    | GPIO4        | 14 mV/LSB     |
 * | 8    | GPIO5        | 14 mV/LSB     |
 * | 9    | GPIO0        | 14 mV/LSB     |
 * | 10   | GPIO2        | 14 mV/LSB     |
 * | 11   | IVBUS1_Mean  | 16.5 mA/LSB   |
 * | 12   | Reserved     | --            |
 *
 * @see TI TPS25751 Technical Reference Manual, ADC_RESULTS register
 */
class TPS25751ADCResults : public TPS25751Register
{
public:
    explicit TPS25751ADCResults() : TPS25751Register(TPS25751Registers::Registers::ADC_RESULTS.size) {}
    explicit TPS25751ADCResults(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::ADC_RESULTS.size) {}
    TPS25751ADCResults(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ADCResults(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get the raw ADCIN1 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawADCIN1() const;

    /**
     * @brief Get the ADCIN1 pin voltage in millivolts
     * @return Voltage on ADCIN1 in mV (raw count * 14 mV/LSB)
     */
    uint16_t adcin1Millivolts() const;

    /**
     * @brief Get the raw ADCIN2 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawADCIN2() const;

    /**
     * @brief Get the ADCIN2 pin voltage in millivolts
     * @return Voltage on ADCIN2 in mV (raw count * 14 mV/LSB)
     */
    uint16_t adcin2Millivolts() const;

    /**
     * @brief Get the raw LDO_3V3 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawLDO3V3() const;

    /**
     * @brief Get the LDO_3V3 pin voltage in millivolts
     * @return Voltage on LDO_3V3 in mV (raw count * 14 mV/LSB)
     */
    uint16_t ldo3v3Millivolts() const;

    /**
     * @brief Get the raw PA_VBUS (VBUS1) pin voltage reading
     * @return Raw 8-bit ADC count (98 mV/LSB)
     */
    uint8_t rawVBUS1() const;

    /**
     * @brief Get the PA_VBUS (VBUS1) pin voltage in millivolts
     * @return Voltage on PA_VBUS in mV (raw count * 98 mV/LSB)
     */
    uint16_t vbus1Millivolts() const;

    /**
     * @brief Get the raw most-recent PP_5V1 current reading
     * @return Raw 8-bit ADC count (16.5 mA/LSB)
     */
    uint8_t rawIVBUS1() const;

    /**
     * @brief Get the most-recent PP_5V1 current in milliamps
     *
     * Computed as `raw * 33 / 2` (16.5 mA/LSB expressed as integer math to
     * avoid floating point on this embedded target). This truncates the 0.5
     * mA half-LSB component for odd raw counts.
     *
     * @return Current through PP_5V1 in mA
     */
    uint16_t ivbus1Milliamps() const;

    /**
     * @brief Get the raw GPIO4 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawGPIO4() const;

    /**
     * @brief Get the GPIO4 pin voltage in millivolts
     * @return Voltage on GPIO4 in mV (raw count * 14 mV/LSB)
     */
    uint16_t gpio4Millivolts() const;

    /**
     * @brief Get the raw GPIO5 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawGPIO5() const;

    /**
     * @brief Get the GPIO5 pin voltage in millivolts
     * @return Voltage on GPIO5 in mV (raw count * 14 mV/LSB)
     */
    uint16_t gpio5Millivolts() const;

    /**
     * @brief Get the raw GPIO0 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawGPIO0() const;

    /**
     * @brief Get the GPIO0 pin voltage in millivolts
     * @return Voltage on GPIO0 in mV (raw count * 14 mV/LSB)
     */
    uint16_t gpio0Millivolts() const;

    /**
     * @brief Get the raw GPIO2 pin voltage reading
     * @return Raw 8-bit ADC count (14 mV/LSB)
     */
    uint8_t rawGPIO2() const;

    /**
     * @brief Get the GPIO2 pin voltage in millivolts
     * @return Voltage on GPIO2 in mV (raw count * 14 mV/LSB)
     */
    uint16_t gpio2Millivolts() const;

    /**
     * @brief Get the raw recent current peak/mean estimate through PP_5V1
     *
     * This is a mean if PORT_CONTROL.EnableCurrentMonitor=1; the field is
     * cleared on attach.
     *
     * @return Raw 8-bit ADC count (16.5 mA/LSB)
     */
    uint8_t rawIVBUS1Mean() const;

    /**
     * @brief Get the recent current peak/mean estimate through PP_5V1 in milliamps
     *
     * Computed as `raw * 33 / 2` (16.5 mA/LSB expressed as integer math to
     * avoid floating point on this embedded target). This truncates the 0.5
     * mA half-LSB component for odd raw counts.
     *
     * @return Current through PP_5V1 in mA
     */
    uint16_t ivbus1MeanMilliamps() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates ADC results register data semantically
     *
     * Every field is an independent ADC channel sample with no cross-field
     * consistency rules to enforce. An all-zero buffer is NOT rejected -- it
     * is the legitimate reset / never-measured state (e.g. the PD controller
     * is in Unconnected Sleep, where the TRM states ADC_RESULTS is not
     * updated), not an error condition.
     *
     * @return true if the register is valid, correctly sized, and not the
     *         all-0xFF bus-error signature
     */
    bool isSemanticallyValid() const override;

private:
    // Bit positions (byte-aligned; each field is a whole byte) per the TRM
    // layout table above -- NOT alphabetical, transcribed exactly.
    static constexpr uint8_t kADCIN1BitPos = 0;
    static constexpr uint8_t kADCIN2BitPos = 8;
    static constexpr uint8_t kLDO3V3BitPos = 16;
    static constexpr uint8_t kVBUS1BitPos = 24;
    // Byte 4 is reserved.
    static constexpr uint8_t kIVBUS1BitPos = 40;
    // Byte 6 is reserved.
    static constexpr uint8_t kGPIO4BitPos = 56;
    static constexpr uint8_t kGPIO5BitPos = 64;
    static constexpr uint8_t kGPIO0BitPos = 72;
    static constexpr uint8_t kGPIO2BitPos = 80;
    static constexpr uint8_t kIVBUS1MeanBitPos = 88;
    // Byte 12 is reserved.

    static constexpr uint8_t kFieldBitLen = 8;

    // Unit conversion constants.
    static constexpr uint16_t kMillivoltsPerLsb14 = 14;
    static constexpr uint16_t kVbus1MillivoltsPerLsb = 98;
    static constexpr uint16_t kMilliampsPerLsbNumerator = 33;    ///< 16.5 mA/LSB == 33/2
    static constexpr uint16_t kMilliampsPerLsbDenominator = 2;
};
