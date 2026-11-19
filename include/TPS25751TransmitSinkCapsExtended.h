#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751UsbPd.h"

/**
 * @file TPS25751TransmitSinkCapsExtended.h
 * @brief TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK register (0x7E) class implementation
 *
 * This register holds the Sink Capabilities Extended Data Block (SKEDB) that the
 * controller sends in reply to a USB PD 3.x Get_Sink_Cap_Extended message.
 *
 * Every field is "as defined in the USB PD specification" (Sink_Capabilities_Extended).
 * The TRM does not restate the encodings, so the raw accessors return the field
 * bits unchanged. The typed accessors beside them decode those bits per USB PD
 * R3.3 Table 6.70 (Sink_Capabilities_Extended Data Block): SKEDB version, load
 * step, the Sink Load Characteristics word, the compliance flags, the
 * touch-temperature standard, the battery counts, the Sink Modes flags, and the
 * three SPR PDP bytes.
 *
 * The TRM numbers its bytes from the start of this register. The USB PD
 * Sink_Capabilities_Extended Data Block (SKEDB) starts with VID, PID, XID, and the
 * firmware and hardware versions, which the controller prepends (see Reply
 * content), so TRM byte n is SKEDB byte n + 10 for n 0 to 10. The three EPR Sink
 * PDP bytes (SKEDB 21 to 23) fall in the TRM's reserved bits 111:88.
 *
 * Reply content: when the controller answers Get_Sink_Cap_Extended it first sends
 * the VID, PID, and XID fields (from the firmware configuration made with the
 * Application Customization Tool), the firmware version, and the hardware version
 * (BOOT_FLAGS (0x2D) REV_ID word), then appends the bytes of this register.
 *
 * Gate bit: PD3_CONFIGURATION (0x42) bit 17, TPS25751PD3Config::supportSinkCapExtended().
 * With the bit clear, the controller ignores this register and answers
 * Get_Sink_Cap_Extended with Not_Supported. The controller takes no automatic
 * action when this register is written.
 *
 * The TRM states: "The values in this register are not used by the PD controller
 * to affect behavior, it just simply uses these contents to respond."
 *
 * Register Details:
 * - Address: 0x7E
 * - Size: 14 bytes (112 bits); the controller's length byte is 14 on hardware
 * - Access: Read/Write
 * - Reset: 0000000000000000000000000000h
 * - Reserved bits: 111:88 (validated to read 0; the firmware did not set any of
 *   them on hardware)
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 14-byte buffer. Multi-byte fields
 * are little-endian. Byte map:
 *   byte 0      SKEDB Version (7:0)
 *   byte 1      Load Step (15:8)
 *   bytes 2-3   Sink Load Characteristics (31:16)
 *   byte 4      Compliance (39:32)
 *   byte 5      Touch Temperature (47:40)
 *   byte 6      Battery Info (55:48)
 *   byte 7      Sink Modes (63:56)
 *   byte 8      Sink Minimum PDP (71:64)
 *   byte 9      Sink Operational PDP (79:72)
 *   byte 10     Sink Maximum PDP (87:80)
 *   bytes 11-13 reserved (111:88)
 *
 * No PDP ordering rule is validated: the TRM gives no encoding for the PDP
 * bytes, so a numeric comparison could reject a valid value.
 *
 * Hardware reading (2026-11-18, board sinking from a PD charger): the register
 * read all zero, the documented reset, and writes round-tripped exactly. On that
 * board PD3_CONFIGURATION bit 17 was 0, so the controller was not answering
 * Get_Sink_Cap_Extended from this register.
 *
 * @code
 * TPS25751 tps;
 * TPS25751TransmitSinkCapsExtended caps;
 * if (tps.readTransmitSinkCapsExtended(caps)) {
 *     caps.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.59
 * @see TPS25751PD3Config
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751TransmitSinkCapsExtended : public TPS25751Register
{
public:
    explicit TPS25751TransmitSinkCapsExtended() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES_EXTENDED.size) {}
    explicit TPS25751TransmitSinkCapsExtended(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES_EXTENDED.size) {}
    TPS25751TransmitSinkCapsExtended(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitSinkCapsExtended(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED;

    // --- Decode (raw values; encodings are defined by the USB PD specification) ---

    /// @brief SKEDB Version (bits 7:0)
    uint8_t skedbVersion() const;
    /// @brief Load Step (bits 15:8)
    uint8_t loadStep() const;
    /// @brief Sink Load Characteristics (bits 31:16)
    uint16_t sinkLoadCharacteristics() const;
    /// @brief Compliance (bits 39:32)
    uint8_t compliance() const;
    /// @brief Touch Temperature (bits 47:40); the TRM spells the field name "Touch Temprature"
    uint8_t touchTemperature() const;
    /// @brief Battery Info (bits 55:48)
    uint8_t batteryInfo() const;
    /// @brief Sink Modes (bits 63:56)
    uint8_t sinkModes() const;
    /// @brief Sink Minimum PDP (bits 71:64)
    uint8_t sinkMinimumPdp() const;
    /// @brief Sink Operational PDP (bits 79:72)
    uint8_t sinkOperationalPdp() const;
    /// @brief Sink Maximum PDP (bits 87:80)
    uint8_t sinkMaximumPdp() const;

    // --- Typed decode, USB PD R3.3 Table 6.70 (built on the raw accessors above) ---

    /// @brief Load step rate, Load Step bits 1:0 (USB PD R3.3 Table 6.70); shared with 0x77, see TPS25751UsbPd.h
    using LoadStep = TPS25751UsbPd::LoadStep;

    /**
     * @brief Touch Temperature standard (USB PD R3.3 Table 6.70)
     *
     * The encoding differs from the Source_Capabilities_Extended one in register
     * 0x77: here 0 is "No applicable standard" and 1 is IEC 60950-1.
     */
    enum class TouchTemperature : uint8_t {
        NoApplicableStandard = 0,  ///< 0 - No applicable standard
        Iec60950_1 = 1,            ///< 1 - IEC 60950-1 (default)
        Iec62368_1Ts1 = 2,         ///< 2 - IEC 62368-1 TS1
        Iec62368_1Ts2 = 3          ///< 3 - IEC 62368-1 TS2
    };

    /// @brief Text for a LoadStep; codes 2 and 3 are "Invalid (assume 150 mA/us)"
    static const char *toString(LoadStep val);
    /// @brief Text for a TouchTemperature; codes 4 to 255 are "Invalid (assume IEC 60950-1)"
    static const char *toString(TouchTemperature val);

    /// @brief True when SKEDB Version is 1 (Version 1.0); 0 and 2 to 255 are invalid and the receiver ignores the message (USB PD R3.3 Table 6.70)
    bool skedbVersionValid() const;

    /**
     * @brief Load step rate, Load Step bits 1:0 (USB PD R3.3 Table 6.70)
     *
     * Named loadStepRate() because loadStep() is the raw byte. Codes 2 and 3 are
     * invalid and the receiver assumes the default; they pass through cast into
     * the enum.
     */
    LoadStep loadStepRate() const;

    /**
     * @brief Sink Load overload in percent, Sink Load Characteristics bits 4:0 (USB PD R3.3 Table 6.70)
     *
     * The field counts 10% steps from 0 to 25; 26 to 31 are invalid and treated
     * as 25. Returns 0 to 250. The specification says to ignore the period and
     * duty cycle when these bits are 0; the period and duty accessors decode
     * them anyway.
     */
    uint16_t sinkLoadOverloadPercent() const;
    /// @brief Sink Load overload period in ms, Sink Load Characteristics bits 10:5 times 20 ms (0 to 1260; USB PD R3.3 Table 6.70)
    uint16_t sinkLoadOverloadPeriod_ms() const;
    /// @brief Sink Load duty cycle in percent, Sink Load Characteristics bits 14:11 times 5 (0 to 75; the field is 4 bits wide, USB PD R3.3 Table 6.70)
    uint8_t sinkLoadDutyCyclePercent() const;
    /// @brief Sink can tolerate VBUS droop of an additional 5 percent during overload, Sink Load Characteristics bit 15 (USB PD R3.3 Table 6.70)
    bool sinkLoadVbusDroopTolerated() const;

    /// @brief LPS compliance, Compliance bit 0 (USB PD R3.3 Table 6.70)
    bool lpsCompliant() const;
    /// @brief PS1 compliance, Compliance bit 1 (USB PD R3.3 Table 6.70)
    bool ps1Compliant() const;
    /// @brief PS2 compliance, Compliance bit 2 (USB PD R3.3 Table 6.70)
    bool ps2Compliant() const;

    /// @brief Touch Temperature standard (USB PD R3.3 Table 6.70); codes above 3 pass through and are invalid
    TouchTemperature touchTemperatureStandard() const;

    /// @brief Count of fixed batteries, Battery Info bits 3:0; values 5 to 15 are invalid and mean 0 (USB PD R3.3 Table 6.70)
    uint8_t fixedBatteryCount() const;
    /// @brief Count of hot swappable battery slots, Battery Info bits 7:4; values 5 to 15 are invalid and mean 0 (USB PD R3.3 Table 6.70)
    uint8_t hotSwappableBatterySlotCount() const;

    /// @brief PPS charging supported, Sink Modes bit 0 (USB PD R3.3 Table 6.70)
    bool ppsChargingSupported() const;
    /// @brief VBUS powered, Sink Modes bit 1 (USB PD R3.3 Table 6.70)
    bool vbusPowered() const;
    /// @brief AC supply powered, Sink Modes bit 2 (USB PD R3.3 Table 6.70)
    bool acSupplyPowered() const;
    /// @brief Battery powered, Sink Modes bit 3 (USB PD R3.3 Table 6.70)
    bool batteryPowered() const;
    /// @brief Battery essentially unlimited, Sink Modes bit 4 (USB PD R3.3 Table 6.70)
    bool batteryEssentiallyUnlimited() const;
    /// @brief AVS supported, Sink Modes bit 5 (USB PD R3.3 Table 6.70)
    bool avsSupported() const;
    /// @brief VCONN powered accessory, Sink Modes bit 6 (USB PD R3.3 Table 6.70); bit 7 is not defined by the specification
    bool vconnPoweredAccessory() const;

    /// @brief True when SPR Sink Minimum PDP is 0 to 100 W; 101 to 255 are invalid (USB PD R3.3 Table 6.70)
    bool sinkMinimumPdpValid() const;
    /// @brief SPR Sink Minimum PDP in watts when valid, else 0
    uint8_t sinkMinimumPdp_W() const;
    /// @brief True when SPR Sink Operational PDP is 0 to 100 W; 101 to 255 are invalid (USB PD R3.3 Table 6.70)
    bool sinkOperationalPdpValid() const;
    /// @brief SPR Sink Operational PDP in watts when valid, else 0
    uint8_t sinkOperationalPdp_W() const;
    /// @brief True when SPR Sink Maximum PDP is 0 to 100 W; 101 to 255 are invalid (USB PD R3.3 Table 6.70)
    bool sinkMaximumPdpValid() const;
    /// @brief SPR Sink Maximum PDP in watts when valid, else 0
    uint8_t sinkMaximumPdp_W() const;

    // --- Setters: each a single setBits() call (setBits() itself guards !isValid()) ---

    /// @brief Set SKEDB Version (bits 7:0)
    void setSkedbVersion(uint8_t value);
    /// @brief Set Load Step (bits 15:8)
    void setLoadStep(uint8_t value);
    /// @brief Set Sink Load Characteristics (bits 31:16)
    void setSinkLoadCharacteristics(uint16_t value);
    /// @brief Set Compliance (bits 39:32)
    void setCompliance(uint8_t value);
    /// @brief Set Touch Temperature (bits 47:40)
    void setTouchTemperature(uint8_t value);
    /// @brief Set Battery Info (bits 55:48)
    void setBatteryInfo(uint8_t value);
    /// @brief Set Sink Modes (bits 63:56)
    void setSinkModes(uint8_t value);
    /// @brief Set Sink Minimum PDP (bits 71:64)
    void setSinkMinimumPdp(uint8_t value);
    /// @brief Set Sink Operational PDP (bits 79:72)
    void setSinkOperationalPdp(uint8_t value);
    /// @brief Set Sink Maximum PDP (bits 87:80)
    void setSinkMaximumPdp(uint8_t value);

    /**
     * @brief Print the 14 raw bytes and every field
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: reserved bits 111:88 read 0. There is no other rule: the TRM
     * gives no encoding for any field, so ordering checks such as minimum PDP at
     * most maximum PDP are not applied.
     */
    bool isSemanticallyValid() const override;

    // --- Bit positions (SLVUCR8B Rev. B, p.59) ---------------------------------

    static constexpr uint8_t kSkedbVersionBit = 0;           ///< bits 7:0
    static constexpr uint8_t kLoadStepBit = 8;               ///< bits 15:8
    static constexpr uint8_t kSinkLoadCharacteristicsBit = 16;  ///< bits 31:16
    static constexpr uint8_t kComplianceBit = 32;            ///< bits 39:32
    static constexpr uint8_t kTouchTemperatureBit = 40;      ///< bits 47:40
    static constexpr uint8_t kBatteryInfoBit = 48;           ///< bits 55:48
    static constexpr uint8_t kSinkModesBit = 56;             ///< bits 63:56
    static constexpr uint8_t kSinkMinimumPdpBit = 64;        ///< bits 71:64
    static constexpr uint8_t kSinkOperationalPdpBit = 72;    ///< bits 79:72
    static constexpr uint8_t kSinkMaximumPdpBit = 80;        ///< bits 87:80
    /// First reserved bit; reserved bits 111:88 are 24 bits wide.
    static constexpr uint8_t kReservedBit = 88;
    static constexpr uint8_t kReservedLen = 24;
};
