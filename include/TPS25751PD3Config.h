#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751PD3Config.h
 * @brief PD3_CONFIGURATION register (0x42) class implementation
 *
 * PD3_CONFIGURATION holds the USB PD 3.0 message-support switches. Each
 * "Support ..." bit enables the controller's reply to one PD 3.0 Get_* message,
 * answered from a transmit-data register (0x77, 0x78, 0x7A, 0x7B, 0x7D, 0x7E).
 * With a bit clear, the controller answers that Get_* message with
 * Not_Supported. The remaining bits turn on automatic Get_Manufacturer_Info and
 * unchunked messaging.
 *
 * Register Details:
 * - Address: 0x42
 * - Size: 4 bytes (32 bits)
 * - Access: Read/Write
 * - Reset: 00080010h (Support Get Revision and Unchunked Supported set)
 * - Reserved bits: 31:21, 16:12, 9, 6:5, 3:0. Validation checks all but 3:0,
 *   which the firmware sets (see kFirmwareReservedMask)
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 4-byte buffer, so the reset value
 * is the buffer {0x10, 0x00, 0x08, 0x00}.
 *
 * @code
 * TPS25751 tps;
 * TPS25751PD3Config pd3;
 * if (tps.readPD3Config(pd3)) {
 *     pd3.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), Section 3.2.24, Table 3-26, p.46
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751PD3Config : public TPS25751Register
{
public:
    explicit TPS25751PD3Config() : TPS25751Register(TPS25751Registers::Registers::PD3_CONFIGURATION.size) {}
    explicit TPS25751PD3Config(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::PD3_CONFIGURATION.size) {}
    TPS25751PD3Config(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751PD3Config(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::PD3_CONFIGURATION;

    // --- Decode ---------------------------------------------------------------

    /**
     * @brief Support PPS Status
     *
     * TRM name "Support PPS Status", Table 3-26, bit 20. Gates the reply to
     * Get_PPS_Status, answered from TX_PPS_SDB (0x7A). With the bit clear the
     * controller answers Get_PPS_Status with Not_Supported.
     */
    bool supportPpsStatus() const;

    /**
     * @brief Support Get Revision
     *
     * TRM name "Support Get Revision", Table 3-26, bit 19. Gates the reply to
     * Get_Revision with the supported PD Spec Version. With the bit clear the
     * controller answers Get_Revision with Not_Supported. Set at reset.
     */
    bool supportGetRevision() const;

    /**
     * @brief Support Get Source Info
     *
     * TRM name "Support Get Source Info", Table 3-26, bit 18. Gates the reply to
     * Get_Source_Info, answered from TX_SOURCE_INFO (0x78). With the bit clear
     * the controller answers Get_Source_Info with Not_Supported.
     */
    bool supportGetSourceInfo() const;

    /**
     * @brief Support Sink Cap Extended
     *
     * TRM name "Support Sink Cap Extended", Table 3-26, bit 17. Gates the reply
     * to Get_Sink_Capabilities_Extended, answered from TX_SKEDB (0x7E). With the
     * bit clear the controller answers it with Not_Supported.
     */
    bool supportSinkCapExtended() const;

    /**
     * @brief Support Battery Status Message
     *
     * TRM name "Support Battery Status Message", Table 3-26, bit 11. Gates the
     * reply to Get_Battery_Status, answered from TX_BSDO (0x7B). With the bit
     * clear the controller answers Get_Battery_Status with Not_Supported.
     */
    bool supportBatteryStatus() const;

    /**
     * @brief Support Battery Capabilities Message
     *
     * TRM name "Support Battery Capabilities Message", Table 3-26, bit 10. Gates
     * the reply to Get_Battery_Capabilities, answered from TX_BCDB (0x7D). With
     * the bit clear the controller answers it with Not_Supported.
     */
    bool supportBatteryCapabilities() const;

    /**
     * @brief Support Source Extended Message
     *
     * TRM name "Support Source Extended Message", Table 3-26, bit 8. Gates the
     * reply to Get_Source_Capabilities_Extended, answered from TX_SCEDB (0x77).
     * With the bit clear the controller answers it with Not_Supported.
     */
    bool supportSourceExtended() const;

    /**
     * @brief Auto Get MIDB
     *
     * TRM name "Auto Get MIDB", Table 3-26, bit 7. When set, the controller
     * automatically sends Get_Manufacturer_Info after receiving Discover
     * Identity; the result is stored in TX_MIBDB_SOP (0x73), which this library
     * does not decode. The companion event is INT_EVENT1 bit 29 (MIDB Received,
     * see TPS25751InterruptEvent).
     *
     * TRM text: "Enable Auto send GetManufaturerInfo" ... "USB PD msesage" (sic).
     */
    bool autoGetMidb() const;

    /**
     * @brief Unchunked Supported
     *
     * TRM name "Unchunked Supported", Table 3-26, bit 4. When set, the
     * controller supports unchunked extended messages up to 260 bytes. The host
     * must consume an unchunked message before the controller can receive
     * another long one. Set at reset.
     */
    bool unchunkedSupported() const;

    // --- Setters: each a single setBits() call, touching only its own bit ------

    /// @brief Set Support PPS Status (bit 20); see supportPpsStatus()
    void setSupportPpsStatus(bool enabled);
    /// @brief Set Support Get Revision (bit 19); see supportGetRevision()
    void setSupportGetRevision(bool enabled);
    /// @brief Set Support Get Source Info (bit 18); see supportGetSourceInfo()
    void setSupportGetSourceInfo(bool enabled);
    /// @brief Set Support Sink Cap Extended (bit 17); see supportSinkCapExtended()
    void setSupportSinkCapExtended(bool enabled);
    /// @brief Set Support Battery Status Message (bit 11); see supportBatteryStatus()
    void setSupportBatteryStatus(bool enabled);
    /// @brief Set Support Battery Capabilities Message (bit 10); see supportBatteryCapabilities()
    void setSupportBatteryCapabilities(bool enabled);
    /// @brief Set Support Source Extended Message (bit 8); see supportSourceExtended()
    void setSupportSourceExtended(bool enabled);
    /// @brief Set Auto Get MIDB (bit 7); see autoGetMidb()
    void setAutoGetMidb(bool enabled);
    /// @brief Set Unchunked Supported (bit 4); see unchunkedSupported()
    void setUnchunkedSupported(bool enabled);

    /**
     * @brief Print the raw value, each named bit, and any non-zero reserved bits
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: reserved bits 31:21, 16:12, 9, and 6:5 must read 0.
     * Reserved bits 3:0 are not checked, because the firmware sets them (see
     * kFirmwareReservedMask).
     */
    bool isSemanticallyValid() const override;

    // --- Bit positions and masks (Table 3-26) ---------------------------------

    static constexpr uint8_t kSupportPpsStatusBit = 20;
    static constexpr uint8_t kSupportGetRevisionBit = 19;
    static constexpr uint8_t kSupportGetSourceInfoBit = 18;
    static constexpr uint8_t kSupportSinkCapExtendedBit = 17;
    static constexpr uint8_t kSupportBatteryStatusBit = 11;
    static constexpr uint8_t kSupportBatteryCapabilitiesBit = 10;
    static constexpr uint8_t kSupportSourceExtendedBit = 8;
    static constexpr uint8_t kAutoGetMidbBit = 7;
    static constexpr uint8_t kUnchunkedSupportedBit = 4;

    /**
     * Mask of the reserved bits that isSemanticallyValid() requires to be 0,
     * derived from Table 3-26:
     *   bits 31:21 = 0xFFE00000
     *   bits 16:12 = 0x0001F000
     *   bit  9     = 0x00000200
     *   bits  6:5  = 0x00000060
     * OR of the four = 0xFFE1F260.
     */
    static constexpr uint32_t kCheckedReservedMask = 0xFFE1F260u;

    /**
     * Reserved bits 3:0, which the TRM gives as reset 0h but the firmware sets.
     * A TPS25751 in APP mode with an EEPROM configuration (Boot Flags Revision
     * ID 193) read 0x0008001A on 2026-11-18: the documented reset plus bits 3
     * and 1. Validation ignores these bits; debugPrint() reports them. The
     * TRM does not say what they mean, so the class does not decode them.
     *
     * kCheckedReservedMask | kFirmwareReservedMask = 0xFFE1F26F, every
     * reserved bit in Table 3-26. Together with the nine defined bits
     * (0x001E0D90) that covers all 32 bits.
     */
    static constexpr uint32_t kFirmwareReservedMask = 0x0000000Fu;
};
