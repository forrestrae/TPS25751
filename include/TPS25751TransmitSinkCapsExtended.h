#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751TransmitSinkCapsExtended.h
 * @brief TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK register (0x7E) class implementation
 *
 * This register holds the Sink Capabilities Extended Data Block (SKEDB) that the
 * controller sends in reply to a USB PD 3.x Get_Sink_Cap_Extended message.
 *
 * Every field is "as defined in the USB PD specification" (Sink_Capabilities_Extended).
 * The TRM does not restate the encodings and the register-docs server does not
 * serve the USB PD specification, so this class exposes raw values only: no
 * units, enums, or bit meanings. Typed decode is planned for Batch L.
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
