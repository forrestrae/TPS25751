#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief Sink PPS request-interval selection
 *
 * Sets the frequency at which the PD controller re-sends a Request message to
 * the source even when the host has not changed the request, while a PPS
 * (Programmable Power Supply) contract is active.
 *
 * @see TI TPS25751 Technical Reference Manual, Table 3-21, "PPS Request Interval"
 */
enum class PpsRequestInterval : uint8_t {
    Interval8s = 0,  ///< 0h - 8 seconds
    Interval4s = 1,  ///< 1h - 4 seconds
    Interval2s = 2,  ///< 2h - 2 seconds
    Interval1s = 3   ///< 3h - 1 second
};

/**
 * @file TPS25751AutoNegotiateSink.h
 * @brief AUTONEGOTIATE_SINK register (0x37) class implementation
 *
 * AUTONEGOTIATE_SINK configures how the PD controller autonomously negotiates
 * sink power contracts: the acceptable voltage window, minimum required
 * power, capability-mismatch handling, and the sink-side PPS (Programmable
 * Power Supply) request parameters. It is "Initialized by Application
 * Customization" per the TRM -- a factory-fresh device may hold whatever
 * value customization tooling wrote, not necessarily all-zero.
 *
 * Register Details:
 * - Address: 0x37
 * - Size: 24 bytes (192 bits; only bits 115:0 are defined -- bits 191:116 are
 *   reserved and untouched by this class's setters)
 * - Access: Read/Write
 *
 * @code
 * TPS25751 tps;
 * auto anSink = tps.readAutoNegotiateSinkRegister();
 * if (anSink) {
 *     anSink->debugPrint();
 *     Serial.print("Min required power (mW): ");
 *     Serial.println(anSink->sinkMinRequiredPower_mW());
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual, Section 3.2.13, Table 3-21
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751AutoNegotiateSink : public TPS25751Register
{
public:
    explicit TPS25751AutoNegotiateSink() : TPS25751Register(TPS25751Registers::Registers::AUTONEGOTIATE_SINK.size) {}
    explicit TPS25751AutoNegotiateSink(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::AUTONEGOTIATE_SINK.size) {}
    TPS25751AutoNegotiateSink(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751AutoNegotiateSink(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::AUTONEGOTIATE_SINK;

    // --- Decode: 1-bit fields (bits 6:0) ---------------------------------------

    /**
     * @brief Get the Auto Neg RDO Priority tie-break selection
     *
     * When two acceptable PDOs of the same type offer the same power, this
     * bit breaks the tie.
     *
     * @return true if the PD controller prefers the LOWER voltage PDO on a tie
     * @return false if the PD controller prefers the HIGHER voltage PDO on a tie (default)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 0
     */
    bool rdoPriorityPrefersLowerVoltage() const;

    /**
     * @brief Get the No USB Suspend flag value used for the RDO's NoUSBSusp bit
     * @return The value inserted as-is into the Request message's NoUSBSusp flag
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 1
     */
    bool noUSBSuspend() const;

    /**
     * @brief Get the Auto Compute Sink Min Power configuration
     * @return true if the PD controller computes the minimum sink power from TX_SINK_CAPS
     * @return false if the minimum sink power is provided by the host (see rawSinkMinRequiredPower())
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 2
     */
    bool autoComputeSinkMinPower() const;

    /**
     * @brief Get the No Capability Mismatch configuration
     *
     * TRM: "No Capabality Mismatch" (SLVUCR8B p.41; TI's spelling).
     * @return true if the Capability Mismatch bit in the RDO is always disabled
     * @return false if Capability Mismatch is enabled (asserted when conditions warrant it)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 3
     */
    bool noCapabilityMismatch() const;

    /**
     * @brief Get the Auto Compute Sink Min Voltage configuration
     * @return true if the PD controller computes the minimum acceptable voltage
     * @return false if the minimum voltage is provided by the host (see rawAutoNegMinVoltage())
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 4
     */
    bool autoComputeSinkMinVoltage() const;

    /**
     * @brief Get the Auto Compute Sink Max Voltage configuration
     * @return true if the PD controller computes the maximum acceptable voltage
     * @return false if the maximum voltage is provided by the host (see rawAutoNegMaxVoltage())
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 5
     */
    bool autoComputeSinkMaxVoltage() const;

    /**
     * @brief Get the Auto Disable Sink Upon Capability Mismatch configuration
     * @return true if the sink path is disabled whenever the negotiated contract
     *         would assert Capability Mismatch (only meaningful if
     *         noCapabilityMismatch() is false)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 6
     */
    bool autoDisableSinkUponCapabilityMismatch() const;

    // --- Decode: scaled multi-bit fields (raw + unit-converted pairs) ----------

    /**
     * @brief Get the raw Auto Neg Sink Min Required Power field
     * @return Raw 10-bit value (250 mW/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 31:22
     */
    uint16_t rawSinkMinRequiredPower() const;

    /**
     * @brief Get the minimum operating power required by the sink, in milliwatts
     * @return sinkMinRequiredPower in mW (raw * 250)
     */
    uint32_t sinkMinRequiredPower_mW() const;

    /**
     * @brief Get the raw Auto Neg Max Current field (bits 21:12)
     *
     * Maximum current the PD controller will request. The host should keep
     * every TX_SINK_CAPS PDO's max current at or below this value. Reset 145h
     * (3250 mA). Source: SLVUCR8B (Rev. B, 2026-08), page 40.
     *
     * @return Raw 10-bit value (10 mA/LSB)
     */
    uint16_t rawAutoNegMaxCurrent() const;

    /**
     * @brief Get the maximum current to request, in milliamps
     * @return autoNegMaxCurrent in mA (raw * 10)
     */
    uint16_t autoNegMaxCurrent_mA() const;

    /**
     * @brief Get the raw Auto Neg Max Voltage field
     * @return Raw 10-bit value (50 mV/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 41:32
     */
    uint16_t rawAutoNegMaxVoltage() const;

    /**
     * @brief Get the maximum voltage the PD controller will request, in millivolts
     * @return autoNegMaxVoltage in mV (raw * 50)
     */
    uint16_t autoNegMaxVoltage_mV() const;

    /**
     * @brief Get the raw Auto Neg Min Voltage field
     * @return Raw 10-bit value (50 mV/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 51:42
     */
    uint16_t rawAutoNegMinVoltage() const;

    /**
     * @brief Get the minimum voltage the PD controller will request, in millivolts
     * @return autoNegMinVoltage in mV (raw * 50)
     */
    uint16_t autoNegMinVoltage_mV() const;

    /**
     * @brief Get the raw Auto Neg Capabilities Mismatch Power field
     * @return Raw 10-bit value (250 mW/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 61:52
     */
    uint16_t rawCapMismatchPower() const;

    /**
     * @brief Get the capability-mismatch power threshold, in milliwatts
     * @return capMismatchPower in mW (raw * 250)
     */
    uint32_t capMismatchPower_mW() const;

    // --- Decode: PPS (Programmable Power Supply) sink fields --------------------

    /**
     * @brief Get the PPS Enable Sink Mode bit
     * @return true if the PD controller attempts to negotiate a PPS sink contract
     *         (prioritized over any other supply type when set)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 64
     */
    bool ppsEnableSinkMode() const;

    /**
     * @brief Get the PPS Request Interval field
     * @return The configured PpsRequestInterval value
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 66:65
     */
    PpsRequestInterval ppsRequestInterval() const;

    /**
     * @brief Get the PPS Operating Mode selection
     *
     * Source: SLVUCR8B (Rev. B, 2026-08), page 40 ("PPS Operating Mode";
     * earlier library versions said "PPS Source Operating Mode").
     * @return true if the system is assumed to be in Constant Voltage (CV) mode
     * @return false if the system is assumed to be in Current-Limit (CC) mode
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 67
     */
    bool ppsOperatingModeConstantVoltage() const;

    /**
     * @brief Get the PPS Required Full Voltage Range configuration
     * @return true if a PPS supply is only selected when the source's APDO fully
     *         covers TX_SINK_CAPS' PPS min/max voltage and max current
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 68
     */
    bool ppsRequiredFullVoltageRange() const;

    /**
     * @brief Get the PPS Disable Sink Upon Non-APDO Contract configuration
     * @return true if the sink path is disabled before requesting a non-PPS
     *         supply type after a PPS contract was previously negotiated
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bit 69
     */
    bool ppsDisableSinkUponNonApdoContract() const;

    /**
     * @brief Get the raw PPS Operating Current field
     * @return Raw 7-bit value (50 mA/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 102:96
     */
    uint8_t rawPpsOperatingCurrent() const;

    /**
     * @brief Get the desired PPS sink operating current, in milliamps
     * @return ppsOperatingCurrent in mA (raw * 50)
     */
    uint16_t ppsOperatingCurrent_mA() const;

    /**
     * @brief Get the raw PPS Operating Voltage field
     *
     * Source: SLVUCR8B (Rev. B, 2026-08), page 40 ("PPS Operating Voltage";
     * earlier library versions said "PPS Output Voltage").
     * @return Raw 11-bit value (20 mV/LSB)
     * @see TI TPS25751 Technical Reference Manual, Table 3-21, bits 115:105
     */
    uint16_t rawPpsOperatingVoltage() const;

    /**
     * @brief Get the desired PPS sink operating voltage, in millivolts
     * @return ppsOperatingVoltage in mV (raw * 20)
     */
    uint16_t ppsOperatingVoltage_mV() const;

    // --- Setters (1:1 with the accessors above; each a single setBits() call) --

    /**
     * @brief Set the Auto Neg RDO Priority tie-break selection
     * @param preferLowerVoltage true to prefer the lower-voltage PDO on a tie
     */
    void setRdoPriorityPrefersLowerVoltage(bool preferLowerVoltage);

    /// @brief Set the No USB Suspend flag used for the RDO's NoUSBSusp bit
    void setNoUSBSuspend(bool value);

    /// @brief Set the Auto Compute Sink Min Power configuration
    void setAutoComputeSinkMinPower(bool computedByController);

    /// @brief Set the No Capability Mismatch configuration
    void setNoCapabilityMismatch(bool disabled);

    /// @brief Set the Auto Compute Sink Min Voltage configuration
    void setAutoComputeSinkMinVoltage(bool computedByController);

    /// @brief Set the Auto Compute Sink Max Voltage configuration
    void setAutoComputeSinkMaxVoltage(bool computedByController);

    /// @brief Set the Auto Disable Sink Upon Capability Mismatch configuration
    void setAutoDisableSinkUponCapabilityMismatch(bool enabled);

    /**
     * @brief Set the raw Auto Neg Sink Min Required Power field
     * @param raw Raw 10-bit value (250 mW/LSB); values above 0x3FF are masked
     */
    void setRawSinkMinRequiredPower(uint16_t raw);

    /**
     * @brief Set the raw Auto Neg Max Current field (bits 21:12)
     * @param raw Raw 10-bit value (10 mA/LSB); values above 0x3FF are masked
     */
    void setRawAutoNegMaxCurrent(uint16_t raw);

    /**
     * @brief Set the raw Auto Neg Max Voltage field
     * @param raw Raw 10-bit value (50 mV/LSB); values above 0x3FF are masked
     */
    void setRawAutoNegMaxVoltage(uint16_t raw);

    /**
     * @brief Set the raw Auto Neg Min Voltage field
     * @param raw Raw 10-bit value (50 mV/LSB); values above 0x3FF are masked
     */
    void setRawAutoNegMinVoltage(uint16_t raw);

    /**
     * @brief Set the raw Auto Neg Capabilities Mismatch Power field
     * @param raw Raw 10-bit value (250 mW/LSB); values above 0x3FF are masked
     */
    void setRawCapMismatchPower(uint16_t raw);

    /// @brief Set the PPS Enable Sink Mode bit
    void setPpsEnableSinkMode(bool enabled);

    /// @brief Set the PPS Request Interval field
    void setPpsRequestInterval(PpsRequestInterval interval);

    /// @brief Set the PPS Operating Mode selection (true = Constant Voltage)
    void setPpsOperatingModeConstantVoltage(bool constantVoltage);

    /// @brief Set the PPS Required Full Voltage Range configuration
    void setPpsRequiredFullVoltageRange(bool required);

    /// @brief Set the PPS Disable Sink Upon Non-APDO Contract configuration
    void setPpsDisableSinkUponNonApdoContract(bool disable);

    /**
     * @brief Set the raw PPS Operating Current field
     * @param raw Raw 7-bit value (50 mA/LSB); values above 0x7F are masked
     */
    void setRawPpsOperatingCurrent(uint8_t raw);

    /**
     * @brief Set the raw PPS Operating Voltage field
     * @param raw Raw 11-bit value (20 mV/LSB); values above 0x7FF are masked
     */
    void setRawPpsOperatingVoltage(uint16_t raw);

    // --- Deprecated pre-Rev. B names: same bits, same units -------------------

    /// @deprecated Use ppsOperatingModeConstantVoltage().
    [[deprecated("SLVUCR8B Rev. B p.40 names bit 67 PPS Operating Mode; use ppsOperatingModeConstantVoltage()")]]
    bool ppsSourceOperatingModeConstantVoltage() const { return ppsOperatingModeConstantVoltage(); }

    /// @deprecated Use setPpsOperatingModeConstantVoltage().
    [[deprecated("SLVUCR8B Rev. B p.40 names bit 67 PPS Operating Mode; use setPpsOperatingModeConstantVoltage()")]]
    void setPpsSourceOperatingModeConstantVoltage(bool constantVoltage) { setPpsOperatingModeConstantVoltage(constantVoltage); }

    /// @deprecated Use rawPpsOperatingVoltage().
    [[deprecated("SLVUCR8B Rev. B p.40 names bits 115:105 PPS Operating Voltage; use rawPpsOperatingVoltage()")]]
    uint16_t rawPpsOutputVoltage() const { return rawPpsOperatingVoltage(); }

    /// @deprecated Use ppsOperatingVoltage_mV().
    [[deprecated("SLVUCR8B Rev. B p.40 names bits 115:105 PPS Operating Voltage; use ppsOperatingVoltage_mV()")]]
    uint16_t ppsOutputVoltage_mV() const { return ppsOperatingVoltage_mV(); }

    /// @deprecated Use setRawPpsOperatingVoltage().
    [[deprecated("SLVUCR8B Rev. B p.40 names bits 115:105 PPS Operating Voltage; use setRawPpsOperatingVoltage()")]]
    void setRawPpsOutputVoltage(uint16_t raw) { setRawPpsOperatingVoltage(raw); }

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates AUTONEGOTIATE_SINK register data semantically
     *
     * Beyond structural validity (correct size, not the all-0xFF bus-error
     * signature), enforces one cross-field rule: when both
     * autoComputeSinkMinVoltage() and autoComputeSinkMaxVoltage() are false
     * (i.e. both bounds are host-provided rather than PD-computed), the
     * host-provided minimum must not exceed the host-provided maximum. This
     * register is "Initialized by Application Customization", so an
     * all-zero buffer is not, by itself, rejected.
     *
     * @return true if the register is valid, correctly sized, and (when
     *         applicable) the host-provided voltage window is well-formed
     */
    bool isSemanticallyValid() const override;

private:
    // Bit positions/widths from the TRM (already converted from high:low
    // ranges to bitPos/bitLen form used by extractBits()/setBits()).
    static constexpr uint8_t kRdoPriorityBit = 0;
    static constexpr uint8_t kNoUSBSuspendBit = 1;
    static constexpr uint8_t kAutoComputeSinkMinPowerBit = 2;
    static constexpr uint8_t kNoCapabilityMismatchBit = 3;
    static constexpr uint8_t kAutoComputeSinkMinVoltageBit = 4;
    static constexpr uint8_t kAutoComputeSinkMaxVoltageBit = 5;
    static constexpr uint8_t kAutoDisableSinkUponCapMismatchBit = 6;

    static constexpr uint8_t kAutoNegMaxCurrentShift = 12;   ///< bits 21:12 (SLVUCR8B p.40)
    static constexpr uint8_t kAutoNegMaxCurrentBits = 10;

    static constexpr uint8_t kSinkMinRequiredPowerShift = 22;
    static constexpr uint8_t kSinkMinRequiredPowerBits = 10;

    static constexpr uint8_t kAutoNegMaxVoltageShift = 32;
    static constexpr uint8_t kAutoNegMaxVoltageBits = 10;

    static constexpr uint8_t kAutoNegMinVoltageShift = 42;
    static constexpr uint8_t kAutoNegMinVoltageBits = 10;

    static constexpr uint8_t kCapMismatchPowerShift = 52;
    static constexpr uint8_t kCapMismatchPowerBits = 10;

    static constexpr uint8_t kPpsEnableSinkModeBit = 64;

    static constexpr uint8_t kPpsRequestIntervalShift = 65;
    static constexpr uint8_t kPpsRequestIntervalBits = 2;

    static constexpr uint8_t kPpsOperatingModeBit = 67;
    static constexpr uint8_t kPpsRequiredFullVoltageRangeBit = 68;
    static constexpr uint8_t kPpsDisableSinkUponNonApdoBit = 69;

    static constexpr uint8_t kPpsOperatingCurrentShift = 96;
    static constexpr uint8_t kPpsOperatingCurrentBits = 7;

    static constexpr uint8_t kPpsOperatingVoltageShift = 105;
    static constexpr uint8_t kPpsOperatingVoltageBits = 11;

    // Unit conversion constants.
    static constexpr uint16_t kMilliwattsPerLsb250 = 250;
    static constexpr uint16_t kMillivoltsPerLsb50 = 50;
    static constexpr uint16_t kMilliampsPerLsb50 = 50;
    static constexpr uint16_t kMilliampsPerLsb10 = 10;
    static constexpr uint16_t kMillivoltsPerLsb20 = 20;
};

// String conversion helper
const char* toString(PpsRequestInterval val);
