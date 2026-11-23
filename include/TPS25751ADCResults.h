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
 * | 3    | VBUS         | 98 mV/LSB     |
 * | 4    | Reserved     | --            |
 * | 5    | IVBUS_Mean   | 16.5 mA/LSB   |
 * | 6    | Reserved     | --            |
 * | 7    | GPIO4        | 14 mV/LSB     |
 * | 8    | GPIO5        | 14 mV/LSB     |
 * | 9    | GPIO0        | 14 mV/LSB     |
 * | 10   | GPIO2        | 14 mV/LSB     |
 * | 11   | IVBUS_Peak   | 16.5 mA/LSB   |
 * | 12   | Reserved     | --            |
 *
 * Source: SLVUCR8B (Rev. B, 2026-08), page 51. IVBUS_Mean and IVBUS_Peak are
 * estimates of the current through the power path, populated only while
 * PORT_CONTROL.EnableCurrentMonitor = 1 and cleared on attach for a new
 * connection. Earlier library versions called byte 5 IVBUS1 ("most recent")
 * and byte 11 IVBUS1_Mean; Rev. B names byte 5 the mean and byte 11 the peak.
 * The old accessors remain as deprecated wrappers that read the same bytes.
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
     * @brief Get the raw VBUS pin voltage reading (byte 3)
     * @return Raw 8-bit ADC count (98 mV/LSB)
     */
    uint8_t rawVBUS() const;

    /**
     * @brief Get the VBUS pin voltage in millivolts (byte 3)
     * @return Voltage on VBUS in mV (raw count * 98 mV/LSB)
     */
    uint16_t vbusMillivolts() const;

    /**
     * @brief Get the raw recent mean current estimate through the power path (byte 5)
     *
     * Populated only while PORT_CONTROL.EnableCurrentMonitor = 1; cleared on
     * attach for a new connection. Source: SLVUCR8B (Rev. B, 2026-08), page 51.
     *
     * @return Raw 8-bit ADC count (16.5 mA/LSB)
     */
    uint8_t rawIVBUSMean() const;

    /**
     * @brief Get the recent mean current estimate through the power path in milliamps (byte 5)
     *
     * Computed as `raw * 33 / 2` (16.5 mA/LSB expressed as integer math to
     * avoid floating point on this embedded target). This truncates the 0.5
     * mA half-LSB component for odd raw counts. Requires
     * PORT_CONTROL.EnableCurrentMonitor = 1.
     *
     * @return Mean current in mA
     */
    uint16_t ivbusMeanMilliamps() const;

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
     * @brief Get the raw recent peak current estimate through the power path (byte 11)
     *
     * Populated only while PORT_CONTROL.EnableCurrentMonitor = 1; cleared on
     * attach for a new connection. Source: SLVUCR8B (Rev. B, 2026-08), page 51
     * (TI's description reads "poewr path").
     *
     * @return Raw 8-bit ADC count (16.5 mA/LSB)
     */
    uint8_t rawIVBUSPeak() const;

    /**
     * @brief Get the recent peak current estimate through the power path in milliamps (byte 11)
     *
     * Computed as `raw * 33 / 2` (16.5 mA/LSB expressed as integer math to
     * avoid floating point on this embedded target). This truncates the 0.5
     * mA half-LSB component for odd raw counts. Requires
     * PORT_CONTROL.EnableCurrentMonitor = 1.
     *
     * @return Peak current in mA
     */
    uint16_t ivbusPeakMilliamps() const;

    // --- Deprecated pre-Rev. B names. Each reads exactly the byte it read
    // before; the message names the Rev. B accessor for that same byte. ---

    /// @deprecated Reads byte 3 (VBUS). Use rawVBUS().
    [[deprecated("reads byte 3, which rev B names VBUS; use rawVBUS()")]]
    uint8_t rawVBUS1() const { return rawVBUS(); }

    /// @deprecated Reads byte 3 (VBUS). Use vbusMillivolts().
    [[deprecated("reads byte 3, which rev B names VBUS; use vbusMillivolts()")]]
    uint16_t vbus1Millivolts() const { return vbusMillivolts(); }

    /// @deprecated Reads byte 5, which Rev. B names IVBUS_Mean. Use rawIVBUSMean().
    [[deprecated("reads byte 5, which rev B names IVBUS_Mean; use rawIVBUSMean()")]]
    uint8_t rawIVBUS1() const { return rawIVBUSMean(); }

    /// @deprecated Reads byte 5, which Rev. B names IVBUS_Mean. Use ivbusMeanMilliamps().
    [[deprecated("reads byte 5, which rev B names IVBUS_Mean; use ivbusMeanMilliamps()")]]
    uint16_t ivbus1Milliamps() const { return ivbusMeanMilliamps(); }

    /// @deprecated Reads byte 11, which Rev. B names IVBUS_Peak (not the mean). Use rawIVBUSPeak().
    [[deprecated("reads byte 11, which rev B names IVBUS_Peak; use rawIVBUSPeak()")]]
    uint8_t rawIVBUS1Mean() const { return rawIVBUSPeak(); }

    /// @deprecated Reads byte 11, which Rev. B names IVBUS_Peak (not the mean). Use ivbusPeakMilliamps().
    [[deprecated("reads byte 11, which rev B names IVBUS_Peak; use ivbusPeakMilliamps()")]]
    uint16_t ivbus1MeanMilliamps() const { return ivbusPeakMilliamps(); }

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
    static constexpr uint8_t kVBUSBitPos = 24;
    // Byte 4 is reserved.
    static constexpr uint8_t kIVBUSMeanBitPos = 40;
    // Byte 6 is reserved.
    static constexpr uint8_t kGPIO4BitPos = 56;
    static constexpr uint8_t kGPIO5BitPos = 64;
    static constexpr uint8_t kGPIO0BitPos = 72;
    static constexpr uint8_t kGPIO2BitPos = 80;
    static constexpr uint8_t kIVBUSPeakBitPos = 88;
    // Byte 12 is reserved.

    static constexpr uint8_t kFieldBitLen = 8;

    // Unit conversion constants.
    static constexpr uint16_t kMillivoltsPerLsb14 = 14;
    static constexpr uint16_t kVbusMillivoltsPerLsb = 98;
    static constexpr uint16_t kMilliampsPerLsbNumerator = 33;    ///< 16.5 mA/LSB == 33/2
    static constexpr uint16_t kMilliampsPerLsbDenominator = 2;
};
