#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751UsbPd.h"

/**
 * @file TPS25751TransmitSourceCapsExtended.h
 * @brief TX_SOURCE_CAPABILITIES_EXTENDED_DATA_BLOCK register (0x77) class implementation
 *
 * This register holds the Source Capabilities Extended Data Block (SCEDB) that
 * the controller sends in reply to a USB PD 3.x Get_Source_Cap_Extended message.
 *
 * Every field is "as defined by the USB PD specification" (Source_Capabilities_Extended).
 * The TRM does not restate the encodings, so the raw accessors return the field
 * bits unchanged. The typed accessors beside them decode those bits per USB PD
 * R3.3 Table 6.59 (Source_Capabilities_Extended Data Block): load step and IoC,
 * hold up time, the compliance and touch-current flags, each Peak Current word,
 * the touch-temperature standard, the source-input flags, and the SPR Source PDP.
 *
 * The TRM numbers its bytes from the start of this register. The USB PD
 * Source_Capabilities_Extended Data Block (SCEDB) starts with VID, PID, XID, and
 * the firmware and hardware versions, which the controller prepends (see Reply
 * content), so TRM byte n is SCEDB byte n + 10 for n 0 to 13. The TRM has no field
 * for SCEDB byte 24 (EPR Source PDP Rating).
 *
 * Reply content: when the controller answers Get_Source_Cap_Extended it first
 * sends the VID, PID, and XID fields (from the firmware configuration made with
 * the Application Customization Tool), the firmware version, and the hardware
 * version (BOOT_FLAGS (0x2D) REV_ID word), then appends the bytes of this
 * register.
 *
 * Gate bit: PD3_CONFIGURATION (0x42) bit 8, TPS25751PD3Config::supportSourceExtended().
 * The TRM calls this bit "SourceCapExtMsg" in this register's description; in
 * register 0x42 it is "Support Source Extended Message". With the bit clear, the
 * controller answers Get_Source_Cap_Extended with Not_Supported.
 *
 * Source Inputs bit 0 can be changed by the Barrel_Jack_Event GPIO event on its
 * rising and falling edges, so a read-back can differ from the value written. The
 * TRM says the host may set bit 1 when it enables that event.
 *
 * Cross-register consistency: the two battery counts should agree with the
 * populated entries in TRANSMITTED_BATTERY_STATUS (0x7B) and TX_BATTERY_CAPABILITIES
 * (0x7D) (TRM pp.57-58). Validation of one register cannot check this.
 *
 * Register Details:
 * - Address: 0x77
 * - Size: 15 bytes (120 bits); the controller's length byte is 15 on hardware
 * - Access: Read/Write
 * - Reset: 000000000000000000000000000000h
 * - Reserved bits: 119:111 (validated to read 0; the firmware did not set any
 *   of them on hardware)
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 15-byte buffer. Multi-byte fields
 * are little-endian. Byte map:
 *   byte 0      Voltage Regulation (7:0)
 *   byte 1      Hold Up Time (15:8)
 *   byte 2      Compliance (23:16)
 *   byte 3      Touch Current (31:24)
 *   bytes 4-5   Peak Current 1 (47:32)
 *   bytes 6-7   Peak Current 2 (63:48)
 *   bytes 8-9   Peak Current 3 (79:64)
 *   byte 10     Touch Temperature (87:80)
 *   byte 11     Source Inputs (95:88)
 *   byte 12     Number Fixed Batteries (3:0), Number Hot Swappable Batteries (7:4)
 *   byte 13     Source PDP (6:0), bit 7 reserved
 *   byte 14     reserved
 *
 * Hardware reading (2026-11-18, board sinking from a PD charger): the register
 * read all zero except Source PDP = 0x7F (127), not the documented reset of 0;
 * the firmware configuration fills that field. Writes round-tripped exactly. On
 * that board PD3_CONFIGURATION bit 8 was 0, so the controller was not answering
 * Get_Source_Cap_Extended from this register.
 *
 * @code
 * TPS25751 tps;
 * TPS25751TransmitSourceCapsExtended caps;
 * if (tps.readTransmitSourceCapsExtended(caps)) {
 *     caps.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.54
 * @see TPS25751PD3Config
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751TransmitSourceCapsExtended : public TPS25751Register
{
public:
    explicit TPS25751TransmitSourceCapsExtended() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED.size) {}
    explicit TPS25751TransmitSourceCapsExtended(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED.size) {}
    TPS25751TransmitSourceCapsExtended(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitSourceCapsExtended(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED;

    /// @brief Largest battery count the TRM allows in either battery field ("Max of 4").
    static constexpr uint8_t kMaxBatteryCount = 4;
    /// @brief Number of Peak Current fields (Peak Current 1 to 3).
    static constexpr uint8_t kPeakCurrentCount = 3;

    // --- Decode (raw values; encodings are defined by the USB PD specification) ---

    /// @brief Voltage Regulation (bits 7:0)
    uint8_t voltageRegulation() const;
    /// @brief Hold Up Time (bits 15:8)
    uint8_t holdUpTime() const;
    /// @brief Compliance (bits 23:16)
    uint8_t compliance() const;
    /// @brief Touch Current (bits 31:24)
    uint8_t touchCurrent() const;
    /**
     * @brief Peak Current n (16 bits)
     * @param index 1 to 3, as the TRM numbers them: 1 is bits 47:32, 2 is bits
     *              63:48, 3 is bits 79:64
     * @return the raw field, or 0 when @p index is 0 or greater than 3
     */
    uint16_t peakCurrent(uint8_t index) const;
    /// @brief Touch Temperature (bits 87:80)
    uint8_t touchTemperature() const;
    /// @brief Source Inputs (bits 95:88); bit 0 may be changed by the Barrel_Jack_Event GPIO event
    uint8_t sourceInputs() const;
    /// @brief Number Fixed Batteries (bits 99:96); the TRM maximum is 4
    uint8_t numberFixedBatteries() const;
    /// @brief Number Hot Swappable Batteries (bits 103:100); the TRM maximum is 4
    uint8_t numberHotSwappableBatteries() const;
    /// @brief Source PDP (bits 110:104, 7 bits)
    uint8_t sourcePdp() const;

    // --- Typed decode, USB PD R3.3 Table 6.59 (built on the raw accessors above) ---

    /// @brief Load step rate, Voltage Regulation bits 1:0 (USB PD R3.3 Table 6.59); shared with 0x7E, see TPS25751UsbPd.h
    using LoadStep = TPS25751UsbPd::LoadStep;

    /// @brief Touch Temperature standard, byte 10 of the SCEDB (USB PD R3.3 Table 6.59)
    enum class TouchTemperature : uint8_t {
        Iec60950_1 = 0,     ///< 0 - IEC 60950-1 (default)
        Iec62368_1Ts1 = 1,  ///< 1 - IEC 62368-1 TS1
        Iec62368_1Ts2 = 2   ///< 2 - IEC 62368-1 TS2
    };

    /// @brief Text for a LoadStep; codes 2 and 3 are "Invalid (assume 150 mA/us)"
    static const char *toString(LoadStep val);
    /// @brief Text for a TouchTemperature; any other code is "Invalid (assume IEC 60950-1)"
    static const char *toString(TouchTemperature val);

    /**
     * @brief Load step rate, Voltage Regulation bits 1:0 (USB PD R3.3 Table 6.59)
     *
     * Codes 2 and 3 are invalid and the receiver assumes the default; they pass
     * through cast into the enum.
     */
    LoadStep loadStep() const;
    /// @brief Overload current ratio in percent, Voltage Regulation bit 2: 25 when 0, 90 when 1 (IoC, USB PD R3.3 Table 6.59)
    uint8_t iocPercent() const;

    /// @brief True when Hold Up Time is nonzero (0 means the feature is not supported; USB PD R3.3 Table 6.59)
    bool holdUpTimeSupported() const;
    /// @brief Hold Up Time in milliseconds: the raw value, 0 when not supported (USB PD R3.3 Table 6.59)
    uint8_t holdUpTime_ms() const;

    /// @brief LPS compliance, Compliance bit 0 (USB PD R3.3 Table 6.59)
    bool lpsCompliant() const;
    /// @brief PS1 compliance, Compliance bit 1 (USB PD R3.3 Table 6.59)
    bool ps1Compliant() const;
    /// @brief PS2 compliance, Compliance bit 2 (USB PD R3.3 Table 6.59)
    bool ps2Compliant() const;

    /// @brief Low touch current EPS, Touch Current bit 0 (USB PD R3.3 Table 6.59)
    bool lowTouchCurrentEps() const;
    /// @brief Ground pin supported, Touch Current bit 1 (USB PD R3.3 Table 6.59)
    bool groundPinSupported() const;
    /// @brief Ground pin intended for protective earth, Touch Current bit 2 (USB PD R3.3 Table 6.59)
    bool groundPinProtectiveEarth() const;

    /**
     * @brief Peak Current n percent overload, word bits 4:0 (USB PD R3.3 Table 6.59)
     *
     * The field counts 10% steps from 0 to 25; 26 to 31 are invalid and treated
     * as 25. Returns 0 to 250 percent, and 0 for an index other than 1, 2, 3.
     */
    uint16_t peakCurrentOverloadPercent(uint8_t index) const;
    /// @brief Peak Current n overload period in ms, word bits 10:5 times 20 ms (0 to 1260); 0 for an index other than 1, 2, 3
    uint16_t peakCurrentOverloadPeriod_ms(uint8_t index) const;
    /**
     * @brief Peak Current n duty cycle in percent, word bits 14:11 times 5 (0 to 75)
     *
     * The specification text says 0 to 31 in 5% steps, but the field is 4 bits
     * wide, so the largest value is 15 (75 percent). Returns 0 for an index other
     * than 1, 2, 3.
     */
    uint8_t peakCurrentDutyCyclePercent(uint8_t index) const;
    /// @brief Peak Current n VBUS droop, word bit 15: true when VBUS may droop an additional 5 percent; false for an index other than 1, 2, 3
    bool peakCurrentVbusDroop(uint8_t index) const;

    /// @brief Touch Temperature standard (USB PD R3.3 Table 6.59); other codes pass through and are invalid
    TouchTemperature touchTemperatureStandard() const;

    /// @brief External Supply present, Source Inputs bit 0 (USB PD R3.3 Table 6.59)
    bool externalSupplyPresent() const;
    /// @brief External Supply unconstrained, Source Inputs bit 1; meaningful only when externalSupplyPresent() is true (USB PD R3.3 Table 6.59)
    bool externalSupplyUnconstrained() const;
    /// @brief Internal Battery present, Source Inputs bit 2 (USB PD R3.3 Table 6.59)
    bool internalBatteryPresent() const;

    /**
     * @brief True when Source PDP is a valid SPR rating (0 to 100 W; USB PD R3.3 Table 6.59)
     *
     * The TRM field is 7 bits and the specification field is 8, so the TRM view
     * cannot hold 101 to 255. The hardware reads 0x7F (127), which is invalid.
     */
    bool sourcePdpValid() const;
    /// @brief SPR Source PDP in watts when valid, else 0 (USB PD R3.3 Table 6.59)
    uint8_t sourcePdp_W() const;

    // --- Setters: each a single setBits() call (setBits() itself guards !isValid()) ---

    /// @brief Set Voltage Regulation (bits 7:0)
    void setVoltageRegulation(uint8_t value);
    /// @brief Set Hold Up Time (bits 15:8)
    void setHoldUpTime(uint8_t value);
    /// @brief Set Compliance (bits 23:16)
    void setCompliance(uint8_t value);
    /// @brief Set Touch Current (bits 31:24)
    void setTouchCurrent(uint8_t value);
    /// @brief Set Peak Current n (index 1 to 3); a no-op for any other index
    void setPeakCurrent(uint8_t index, uint16_t value);
    /// @brief Set Touch Temperature (bits 87:80)
    void setTouchTemperature(uint8_t value);
    /// @brief Set Source Inputs (bits 95:88)
    void setSourceInputs(uint8_t value);
    /**
     * @brief Set Number Fixed Batteries (bits 99:96)
     *
     * Writes the raw field, masked to 4 bits so it cannot reach the neighbouring
     * nibble. It does not clamp to 4; validation reports values above 4.
     */
    void setNumberFixedBatteries(uint8_t value);
    /**
     * @brief Set Number Hot Swappable Batteries (bits 103:100)
     *
     * Writes the raw field, masked to 4 bits so it cannot reach the neighbouring
     * nibble. It does not clamp to 4; validation reports values above 4.
     */
    void setNumberHotSwappableBatteries(uint8_t value);
    /// @brief Set Source PDP (bits 110:104); masked to 7 bits so it cannot reach reserved bit 111
    void setSourcePdp(uint8_t value);

    /**
     * @brief Print the 15 raw bytes and every field
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: reserved bits 119:111 read 0, and each battery count is at
     * most kMaxBatteryCount. No other rule, because the TRM defers every field
     * encoding to the USB PD specification.
     */
    bool isSemanticallyValid() const override;

    // --- Bit positions (SLVUCR8B Rev. B, p.54) ---------------------------------

    static constexpr uint8_t kVoltageRegulationBit = 0;          ///< bits 7:0
    static constexpr uint8_t kHoldUpTimeBit = 8;                 ///< bits 15:8
    static constexpr uint8_t kComplianceBit = 16;                ///< bits 23:16
    static constexpr uint8_t kTouchCurrentBit = 24;              ///< bits 31:24
    static constexpr uint8_t kPeakCurrent1Bit = 32;              ///< bits 47:32; each next field is 16 bits higher
    static constexpr uint8_t kTouchTemperatureBit = 80;          ///< bits 87:80
    static constexpr uint8_t kSourceInputsBit = 88;              ///< bits 95:88
    static constexpr uint8_t kNumberFixedBatteriesBit = 96;      ///< bits 99:96, 4 bits
    static constexpr uint8_t kNumberHotSwappableBatteriesBit = 100;  ///< bits 103:100, 4 bits
    static constexpr uint8_t kSourcePdpBit = 104;                ///< bits 110:104, 7 bits
    /// First reserved bit; reserved bits 119:111 are 9 bits wide.
    static constexpr uint8_t kReservedBit = 111;
    static constexpr uint8_t kReservedLen = 9;
};
