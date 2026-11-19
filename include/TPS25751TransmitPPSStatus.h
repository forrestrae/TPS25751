#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751TransmitPPSStatus.h
 * @brief TRANSMITTED_PPS_STATUS_DATA_BLOCK register (0x7A) class implementation
 *
 * This register holds the PPS Status Data Block that the controller sends in
 * reply to a USB PD 3.x Get_PPS_Status message: the PPS output voltage, the
 * output current, the present temperature flag, and the operating mode flag.
 *
 * Gate bit: PD3_CONFIGURATION (0x42) bit 20, TPS25751PD3Config::supportPpsStatus().
 * With that bit clear, the controller ignores this register and answers
 * Get_PPS_Status with Not_Supported. With it set, the controller sends the
 * contents of this register.
 *
 * The TRM labels the block's data object "(BSDO)" in its description. That is a
 * copy error from TX_BSDO (0x7B), the Battery Status Data Object; this register
 * holds the PPS Status Data Block.
 *
 * Units: the TRM gives 20 mV per LSB for the output voltage and 50 mA per LSB for
 * the output current. It describes both fields as "as defined by the USB PD
 * spec"; the units here are the TRM's. Raw 0xFFFF (voltage) and 0xFF (current)
 * mean "field not supported", so the mV and mA accessors return 0 for them and
 * the *Supported() accessors report the distinction.
 *
 * TRM gap: Present Temperature Flag 00b, which is the reset value, has no stated
 * meaning. The TRM defines only 01b (normal), 10b (warning zone), and 11b
 * (over-temperature).
 *
 * Register Details:
 * - Address: 0x7A
 * - Size: 4 bytes (32 bits); the controller's length byte is 4 on hardware
 * - Access: Read/Write
 * - Reset: 00000000h
 * - Reserved bits: 31:28 and 24 (validated to read 0; the firmware did not set
 *   any of them on hardware)
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 4-byte buffer, so the voltage is
 * bytes 0 and 1 (little-endian), the current is byte 2, and the flags are in
 * byte 3.
 *
 * Hardware reading (2026-11-18, board sinking from a PD charger): the register
 * read 00 00 00 00, and writes round-tripped exactly. On that board
 * PD3_CONFIGURATION bit 20 was 0, so the controller was not answering
 * Get_PPS_Status from this register.
 *
 * @code
 * TPS25751 tps;
 * TPS25751TransmitPPSStatus pps;
 * if (tps.readTransmitPPSStatus(pps)) {
 *     pps.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.56
 * @see TPS25751PD3Config
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751TransmitPPSStatus : public TPS25751Register
{
public:
    explicit TPS25751TransmitPPSStatus() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_PPS_STATUS.size) {}
    explicit TPS25751TransmitPPSStatus(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_PPS_STATUS.size) {}
    TPS25751TransmitPPSStatus(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitPPSStatus(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_PPS_STATUS;

    // --- Units and sentinels --------------------------------------------------

    /// @brief Output Voltage (bits 15:0): 20 mV per LSB.
    static constexpr uint16_t kVoltageLsb_mV = 20;
    /// @brief Output Current (bits 23:16): 50 mA per LSB.
    static constexpr uint16_t kCurrentLsb_mA = 50;
    /// @brief Raw Output Voltage meaning "this field not supported".
    static constexpr uint16_t kVoltageNotSupported = 0xFFFF;
    /// @brief Raw Output Current meaning "this field not supported".
    static constexpr uint8_t kCurrentNotSupported = 0xFF;

    /// @brief Present Temperature Flag (bits 26:25)
    enum class PresentTemperatureFlag : uint8_t {
        NotDefined = 0,      ///< 00b - not defined in the TRM (the reset value)
        Normal = 1,          ///< 01b - temperature is normal
        Warning = 2,         ///< 10b - warning zone
        OverTemperature = 3  ///< 11b - over-temperature
    };

    /// @brief Operating Mode Flag (bit 27)
    enum class OperatingMode : uint8_t {
        ConstantVoltage = 0,  ///< 0 - running in Constant Voltage (CV) mode
        CurrentLimit = 1      ///< 1 - running in Current Limit (CL) mode
    };

    /// @brief Human-readable text for a PresentTemperatureFlag value.
    static const char *toString(PresentTemperatureFlag val);
    /// @brief Human-readable text for an OperatingMode value.
    static const char *toString(OperatingMode val);

    // --- Decode ---------------------------------------------------------------

    /// @brief Raw Output Voltage (TRM "Output Voltage", bits 15:0); 0xFFFF means not supported
    uint16_t rawOutputVoltage() const;
    /// @brief true unless the raw Output Voltage is the not-supported sentinel 0xFFFF
    bool outputVoltageSupported() const;
    /**
     * @brief Output Voltage in mV (raw x 20)
     * @return the voltage, or 0 when the field is not supported (raw 0xFFFF).
     *         Check outputVoltageSupported() to tell 0 V from "not supported".
     */
    uint32_t outputVoltage_mV() const;

    /// @brief Raw Output Current (TRM "Output Current", bits 23:16); 0xFF means not supported
    uint8_t rawOutputCurrent() const;
    /// @brief true unless the raw Output Current is the not-supported sentinel 0xFF
    bool outputCurrentSupported() const;
    /**
     * @brief Output Current in mA (raw x 50)
     * @return the current, or 0 when the field is not supported (raw 0xFF).
     *         Check outputCurrentSupported() to tell 0 A from "not supported".
     */
    uint16_t outputCurrent_mA() const;

    /// @brief Present Temperature Flag (TRM "PTF", bits 26:25); 00b is not defined in the TRM
    PresentTemperatureFlag presentTemperatureFlag() const;
    /// @brief Raw Present Temperature Flag code (bits 26:25)
    uint8_t rawPresentTemperatureFlag() const;

    /// @brief Operating Mode Flag (TRM "OMF", bit 27): CV (0) or CL (1)
    OperatingMode operatingMode() const;

    // --- Setters: each a single setBits() call (setBits() itself guards !isValid()) ---

    /// @brief Set raw Output Voltage (bits 15:0); may write the not-supported sentinel 0xFFFF
    void setRawOutputVoltage(uint16_t raw);
    /// @brief Set raw Output Current (bits 23:16); may write the not-supported sentinel 0xFF
    void setRawOutputCurrent(uint8_t raw);

    /**
     * @brief Set Output Voltage from mV
     *
     * Stores raw = mV / 20, rounded down (floor, as the library's PDO builders
     * do), clamped to 0xFFFE. An over-range input saturates at 0xFFFE (1 310 680
     * mV) and never produces the not-supported sentinel 0xFFFF; use
     * setOutputVoltageNotSupported() for that.
     */
    void setOutputVoltage_mV(uint32_t mV);

    /**
     * @brief Set Output Current from mA
     *
     * Stores raw = mA / 50, rounded down (floor), clamped to 0xFE. An over-range
     * input saturates at 0xFE (12 700 mA) and never produces the not-supported
     * sentinel 0xFF; use setOutputCurrentNotSupported() for that.
     */
    void setOutputCurrent_mA(uint32_t mA);

    /// @brief Mark Output Voltage not supported (writes raw 0xFFFF)
    void setOutputVoltageNotSupported();
    /// @brief Mark Output Current not supported (writes raw 0xFF)
    void setOutputCurrentNotSupported();

    /// @brief Set Present Temperature Flag (bits 26:25)
    void setPresentTemperatureFlag(PresentTemperatureFlag flag);
    /// @brief Set Operating Mode Flag (bit 27)
    void setOperatingMode(OperatingMode mode);

    /**
     * @brief Print the raw value, voltage, current, temperature flag, and mode
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: reserved bits 31:28 and 24 read 0. Present Temperature Flag 00b
     * is accepted because it is the reset value. There is no other rule.
     */
    bool isSemanticallyValid() const override;

    // --- Bit positions and masks (SLVUCR8B Rev. B, p.56) ----------------------

    static constexpr uint8_t kOutputVoltageBit = 0;             ///< bits 15:0, 16 bits
    static constexpr uint8_t kOutputCurrentBit = 16;            ///< bits 23:16, 8 bits
    static constexpr uint8_t kPresentTemperatureFlagBit = 25;   ///< bits 26:25, 2 bits
    static constexpr uint8_t kOperatingModeBit = 27;

    /**
     * Reserved bits 31:28 and 24, derived from the TRM table:
     *   bits 31:28 = 0xF0000000
     *   bit  24    = 0x01000000
     * OR of the two = 0xF1000000.
     */
    static constexpr uint32_t kReservedMask = 0xF1000000u;
};
