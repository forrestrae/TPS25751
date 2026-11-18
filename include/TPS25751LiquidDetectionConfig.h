#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751LiquidDetectionConfig.h
 * @brief LIQUID_DETECTION_CONFIG register (0x98) class implementation
 *
 * LIQUID_DETECTION_CONFIG configures liquid/moisture detection on the SBU pins
 * (via external detection circuitry) and the corrosion-mitigation response --
 * the wait intervals used while polling with and without liquid present, the
 * ADC sample count and cadence, the ADC threshold pairs (each with hysteresis
 * between the "No Liquid" and "Liquid" states), and the state/enable bits that
 * arm liquid detection and corrosion mitigation.
 *
 * Register Details:
 * - Address: 0x98
 * - Size: 11 bytes (88 bits; only bits 82:0 are defined -- bits 87:83 are
 *   reserved and untouched by this class's setters)
 * - Access: Read/Write
 * - Reset: all-zero
 *
 * @code
 * TPS25751 tps;
 * auto liquidCfg = tps.readLiquidDetectionConfigRegister();
 * if (liquidCfg) {
 *     liquidCfg->debugPrint();
 *     Serial.print("High threshold (Liquid), mV: ");
 *     Serial.println(liquidCfg->highThresholdLiquid_mV());
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual, Table 3-29
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751LiquidDetectionConfig : public TPS25751Register
{
public:
    explicit TPS25751LiquidDetectionConfig() : TPS25751Register(TPS25751Registers::Registers::LIQUID_DETECTION_CONFIG.size) {}
    explicit TPS25751LiquidDetectionConfig(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::LIQUID_DETECTION_CONFIG.size) {}
    TPS25751LiquidDetectionConfig(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751LiquidDetectionConfig(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::LIQUID_DETECTION_CONFIG;

    // --- Decode: wait-interval fields (raw + unit-converted pairs) ------------

    /**
     * @brief Get the raw Wait Time In Sec Non-Liquid field
     *
     * Source: SLVUCR8B (Rev. B, 2026-08), page 60 ("Wait Time In Sec
     * Non-Liquid"). Rev. A called this field "Sleep Time In Sec No Liquid";
     * position and units are unchanged.
     * @return Raw 16-bit value (1000 ms/LSB); the wait interval used while
     *         polling and no liquid has been detected
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 15:0
     */
    uint16_t rawWaitTimeNoLiquid() const;

    /**
     * @brief Get the No-Liquid wait interval, in milliseconds
     * @return waitTimeNoLiquid in ms (raw * 1000)
     */
    uint32_t waitTimeNoLiquid_ms() const;

    /**
     * @brief Get the raw Wait Time In Sec Liquid field
     *
     * Source: SLVUCR8B (Rev. B, 2026-08), page 60. Rev. A called this field
     * "Sleep Time In Sec Liquid"; position and units are unchanged.
     * @return Raw 16-bit value (1000 ms/LSB); the wait interval used while
     *         polling once liquid has been detected
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 31:16
     */
    uint16_t rawWaitTimeLiquid() const;

    /**
     * @brief Get the Liquid-detected wait interval, in milliseconds
     * @return waitTimeLiquid in ms (raw * 1000)
     */
    uint32_t waitTimeLiquid_ms() const;

    // --- Decode: sampling configuration -----------------------------------------

    /**
     * @brief Get the Number of Samples field
     *
     * The number of ADC samples averaged per measurement. Per the TRM this
     * "must be 2^N to take average"; the reset value is 0, so that constraint
     * is intentionally NOT enforced by this class's validation.
     *
     * @return Raw 8-bit sample count
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 39:32
     */
    uint8_t numberOfSamples() const;

    // --- Decode: ADC threshold fields (raw + unit-converted pairs) -------------

    /**
     * @brief Get the raw Low Threshold ADC No Liquid field
     * @return Raw 8-bit value (14 mV/LSB); the lower hysteresis bound for
     *         exiting the Liquid Detected state
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 47:40
     */
    uint8_t rawLowThresholdNoLiquid() const;

    /// @brief Get the Low Threshold No Liquid, in millivolts (raw * 14)
    uint16_t lowThresholdNoLiquid_mV() const;

    /**
     * @brief Get the raw High Threshold ADC No Liquid field
     * @return Raw 8-bit value (14 mV/LSB); the upper hysteresis bound for
     *         exiting the Liquid Detected state
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 55:48
     */
    uint8_t rawHighThresholdNoLiquid() const;

    /// @brief Get the High Threshold No Liquid, in millivolts (raw * 14)
    uint16_t highThresholdNoLiquid_mV() const;

    /**
     * @brief Get the raw Low Threshold ADC Liquid field
     * @return Raw 8-bit value (14 mV/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 63:56
     */
    uint8_t rawLowThresholdLiquid() const;

    /// @brief Get the Low Threshold Liquid, in millivolts (raw * 14)
    uint16_t lowThresholdLiquid_mV() const;

    /**
     * @brief Get the raw High Threshold ADC Liquid field
     * @return Raw 8-bit value (14 mV/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 71:64
     */
    uint8_t rawHighThresholdLiquid() const;

    /// @brief Get the High Threshold Liquid, in millivolts (raw * 14)
    uint16_t highThresholdLiquid_mV() const;

    // --- Decode: ADC sample-time fields (raw + unit-converted pairs) -----------

    /**
     * @brief Get the raw Sample Time in 10ms No Liquid field
     * @return Raw 4-bit value (10 ms/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 75:72
     */
    uint8_t rawSampleTimeNoLiquid() const;

    /// @brief Get the No-Liquid ADC sample interval, in milliseconds (raw * 10)
    uint16_t sampleTimeNoLiquid_ms() const;

    /**
     * @brief Get the raw Sample Time in 10ms Liquid field
     * @return Raw 4-bit value (10 ms/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bits 79:76
     */
    uint8_t rawSampleTimeLiquid() const;

    /// @brief Get the Liquid ADC sample interval, in milliseconds (raw * 10)
    uint16_t sampleTimeLiquid_ms() const;

    // --- Decode: state / enable bits ---------------------------------------------

    /**
     * @brief Get the Liquid Detection State bit
     * @return true if the register's current state is Liquid Detected
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bit 80
     */
    bool liquidDetectionState() const;

    /**
     * @brief Get the Enable Corrosion Mitigation configuration
     * @return true if corrosion mitigation is enabled -- upon liquid
     *         detection the port is disabled/disconnected and the CC pins
     *         are pulled down
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bit 81
     */
    bool enableCorrosionMitigation() const;

    /**
     * @brief Get the Enable Liquid Detection configuration
     * @return true if liquid detection is enabled (requires proper external
     *         liquid-detection circuitry on the SBU pins)
     * @see TI TPS25751 Technical Reference Manual, Table 3-29, bit 82
     */
    bool enableLiquidDetection() const;

    // --- Setters (1:1 with the accessors above; each a single setBits() call) --

    /**
     * @brief Set the raw Wait Time In Sec No Liquid field
     * @param raw Raw 16-bit value (1000 ms/LSB)
     */
    void setRawWaitTimeNoLiquid(uint16_t raw);

    /**
     * @brief Set the raw Wait Time In Sec Liquid field
     * @param raw Raw 16-bit value (1000 ms/LSB)
     */
    void setRawWaitTimeLiquid(uint16_t raw);

    /// @brief Set the Number of Samples field
    void setNumberOfSamples(uint8_t raw);

    // --- Deprecated pre-Rev. B names: same bits, same units -------------------

    /// @deprecated Rev. A field name. Use rawWaitTimeNoLiquid().
    [[deprecated("SLVUCR8B Rev. B p.60 names bits 15:0 Wait Time In Sec Non-Liquid; use rawWaitTimeNoLiquid()")]]
    uint16_t rawSleepTimeNoLiquid() const { return rawWaitTimeNoLiquid(); }

    /// @deprecated Rev. A field name. Use waitTimeNoLiquid_ms().
    [[deprecated("SLVUCR8B Rev. B p.60 names bits 15:0 Wait Time In Sec Non-Liquid; use waitTimeNoLiquid_ms()")]]
    uint32_t sleepTimeNoLiquid_ms() const { return waitTimeNoLiquid_ms(); }

    /// @deprecated Rev. A field name. Use rawWaitTimeLiquid().
    [[deprecated("SLVUCR8B Rev. B p.60 names bits 31:16 Wait Time In Sec Liquid; use rawWaitTimeLiquid()")]]
    uint16_t rawSleepTimeLiquid() const { return rawWaitTimeLiquid(); }

    /// @deprecated Rev. A field name. Use waitTimeLiquid_ms().
    [[deprecated("SLVUCR8B Rev. B p.60 names bits 31:16 Wait Time In Sec Liquid; use waitTimeLiquid_ms()")]]
    uint32_t sleepTimeLiquid_ms() const { return waitTimeLiquid_ms(); }

    /// @deprecated Rev. A field name. Use setRawWaitTimeNoLiquid().
    [[deprecated("SLVUCR8B Rev. B p.60 names bits 15:0 Wait Time In Sec Non-Liquid; use setRawWaitTimeNoLiquid()")]]
    void setRawSleepTimeNoLiquid(uint16_t raw) { setRawWaitTimeNoLiquid(raw); }

    /// @deprecated Rev. A field name. Use setRawWaitTimeLiquid().
    [[deprecated("SLVUCR8B Rev. B p.60 names bits 31:16 Wait Time In Sec Liquid; use setRawWaitTimeLiquid()")]]
    void setRawSleepTimeLiquid(uint16_t raw) { setRawWaitTimeLiquid(raw); }

    /**
     * @brief Set the raw Low Threshold ADC No Liquid field
     * @param raw Raw 8-bit value (14 mV/LSB)
     */
    void setRawLowThresholdNoLiquid(uint8_t raw);

    /**
     * @brief Set the raw High Threshold ADC No Liquid field
     * @param raw Raw 8-bit value (14 mV/LSB)
     */
    void setRawHighThresholdNoLiquid(uint8_t raw);

    /**
     * @brief Set the raw Low Threshold ADC Liquid field
     * @param raw Raw 8-bit value (14 mV/LSB)
     */
    void setRawLowThresholdLiquid(uint8_t raw);

    /**
     * @brief Set the raw High Threshold ADC Liquid field
     * @param raw Raw 8-bit value (14 mV/LSB)
     */
    void setRawHighThresholdLiquid(uint8_t raw);

    /**
     * @brief Set the raw Sample Time in 10ms No Liquid field
     * @param raw Raw 4-bit value (10 ms/LSB); values above 0xF are masked
     */
    void setRawSampleTimeNoLiquid(uint8_t raw);

    /**
     * @brief Set the raw Sample Time in 10ms Liquid field
     * @param raw Raw 4-bit value (10 ms/LSB); values above 0xF are masked
     */
    void setRawSampleTimeLiquid(uint8_t raw);

    /// @brief Set the Liquid Detection State bit
    void setLiquidDetectionState(bool detected);

    /// @brief Set the Enable Corrosion Mitigation configuration
    void setEnableCorrosionMitigation(bool enabled);

    /// @brief Set the Enable Liquid Detection configuration
    void setEnableLiquidDetection(bool enabled);

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates LIQUID_DETECTION_CONFIG register data semantically
     *
     * Beyond structural validity (correct size, not the all-0xFF bus-error
     * signature), enforces two cross-field rules mirroring the ADC threshold
     * pairs' hysteresis intent: the Low Threshold must not exceed the High
     * Threshold, for both the No-Liquid pair and the Liquid pair. Reset is
     * all-zero, which trivially satisfies both rules.
     *
     * @return true if the register is valid, correctly sized, and both
     *         threshold pairs are well-formed (low <= high)
     */
    bool isSemanticallyValid() const override;

private:
    // Bit positions/widths from the TRM (already converted from high:low
    // ranges to bitPos/bitLen form used by extractBits()/setBits()).
    static constexpr uint8_t kWaitTimeNoLiquidShift = 0;
    static constexpr uint8_t kWaitTimeNoLiquidBits = 16;

    static constexpr uint8_t kWaitTimeLiquidShift = 16;
    static constexpr uint8_t kWaitTimeLiquidBits = 16;

    static constexpr uint8_t kNumberOfSamplesShift = 32;
    static constexpr uint8_t kNumberOfSamplesBits = 8;

    static constexpr uint8_t kLowThresholdNoLiquidShift = 40;
    static constexpr uint8_t kLowThresholdNoLiquidBits = 8;

    static constexpr uint8_t kHighThresholdNoLiquidShift = 48;
    static constexpr uint8_t kHighThresholdNoLiquidBits = 8;

    static constexpr uint8_t kLowThresholdLiquidShift = 56;
    static constexpr uint8_t kLowThresholdLiquidBits = 8;

    static constexpr uint8_t kHighThresholdLiquidShift = 64;
    static constexpr uint8_t kHighThresholdLiquidBits = 8;

    static constexpr uint8_t kSampleTimeNoLiquidShift = 72;
    static constexpr uint8_t kSampleTimeNoLiquidBits = 4;

    static constexpr uint8_t kSampleTimeLiquidShift = 76;
    static constexpr uint8_t kSampleTimeLiquidBits = 4;

    static constexpr uint8_t kLiquidDetectionStateBit = 80;
    static constexpr uint8_t kEnableCorrosionMitigationBit = 81;
    static constexpr uint8_t kEnableLiquidDetectionBit = 82;

    // Unit conversion constants.
    static constexpr uint16_t kMillisecondsPerLsb1000 = 1000;
    static constexpr uint8_t kMillivoltsPerLsb14 = 14;
    static constexpr uint8_t kMillisecondsPerLsb10 = 10;
};
