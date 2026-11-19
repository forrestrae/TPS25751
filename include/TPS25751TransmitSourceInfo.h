#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751TransmitSourceInfo.h
 * @brief TX_SOURCE_INFO register (0x78) class implementation
 *
 * TX_SOURCE_INFO holds the Source_Info data object that the controller sends in
 * reply to a USB PD 3.x Get_Source_Info message. It reports the port type
 * (Managed or Guaranteed Capability Port), whether the port is a DPS port, and
 * the port's power budget as Port Maximum PDP, Port Present PDP, and Port
 * Reported PDP (1 W per LSB in the lower word) plus Port Maximum PDP and Port
 * Guaranteed PDP in 0.5 W steps (the upper word).
 *
 * Gate bit: PD3_CONFIGURATION (0x42) bit 18, TPS25751PD3Config::supportGetSourceInfo().
 * With that bit clear, the controller ignores this register and answers
 * Get_Source_Info with Not_Supported. With it set, the controller sends the
 * contents of this register.
 *
 * TRM text: "This register is automatically updated by the PD firmware and does
 * not require any EC implementation." The firmware may therefore overwrite a host
 * write. Every field is nonetheless listed R/W in the TRM, so the class provides
 * setters and a write path.
 *
 * Specification mapping (USB PD R3.3 V1.0, Tables 6.29 and 6.30, page 154; the
 * tps25751-docs server's Specification block for 78h): the register holds the two
 * Source_Info Data Objects. Bits 31:0 are SIDO1 (Table 6.29, PDP in 1 W steps)
 * and bits 63:32 are SIDO2 (Table 6.30, PDP in 0.5 W steps). Every field lands on
 * the specification field of the same name and width. This settles the two TRM
 * ambiguities:
 * - The two Port Type bits are bit 31 of SIDO1 and bit 31 of SIDO2; each object
 *   carries its own copy. Table 6.30 says that with DPS Port set, Port Type shall
 *   be 0 in both SIDO1 and SIDO2. Semantic validation checks only bit 63 (the TRM
 *   rule).
 * - Table 6.29 says SIDO1 Port Maximum PDP shall equal SIDO2 Port Maximum PDP
 *   rounded down to 1 W. Semantic validation does not check this.
 * Table 6.30 defines Port Guaranteed PDP as the minimum the port can always
 * provide and Port Maximum PDP as the most it will provide, which supports the
 * Guaranteed at most Maximum check below; neither table states that rule.
 *
 * Register Details:
 * - Address: 0x78
 * - Size: 8 bytes (64 bits); the controller's length byte is 8 on hardware
 * - Access: Read/Write
 * - Reset: 8000000080000000h (both Port Type bits set, every PDP field 0)
 * - Reserved bits: 61:50 and 30:24 (validated to read 0; the firmware did not
 *   set any of them on hardware)
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 8-byte buffer, so the reset value
 * is the buffer {0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80}.
 *
 * Hardware reading (2026-11-18, board sinking from a PD charger): the register
 * read back the documented reset value, and writes round-tripped exactly. On that
 * board PD3_CONFIGURATION bit 18 was 0, so the controller was not answering
 * Get_Source_Info from this register.
 *
 * @code
 * TPS25751 tps;
 * TPS25751TransmitSourceInfo info;
 * if (tps.readTransmitSourceInfo(info)) {
 *     info.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.55
 * @see TPS25751PD3Config
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751TransmitSourceInfo : public TPS25751Register
{
public:
    explicit TPS25751TransmitSourceInfo() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_SOURCE_INFO.size) {}
    explicit TPS25751TransmitSourceInfo(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_SOURCE_INFO.size) {}
    TPS25751TransmitSourceInfo(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitSourceInfo(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_SOURCE_INFO;

    // --- Units and field limits -----------------------------------------------

    /// @brief Upper-word PDP fields (bits 49:41 and 40:32): 0.5 W per LSB, in mW.
    static constexpr uint16_t kHalfWattLsb_mW = 500;
    /// @brief Lower-word PDP fields (bits 23:16, 15:8, 7:0): 1 W per LSB, in mW.
    static constexpr uint16_t kWattLsb_mW = 1000;
    /// @brief Largest raw value of a 9-bit PDP field (bits 49:41 and 40:32).
    static constexpr uint16_t kMax9BitRaw = 0x1FF;
    /// @brief Largest raw value of an 8-bit PDP field (bits 23:16, 15:8, 7:0).
    static constexpr uint16_t kMax8BitRaw = 0xFF;

    /// @brief Managed or Guaranteed Capability Port (bits 63 and 31)
    enum class PortType : uint8_t {
        Managed = 0,     ///< 0 - Managed Capability Port
        Guaranteed = 1   ///< 1 - Guaranteed Capability Port
    };

    /// @brief Human-readable text for a PortType value.
    static const char *toString(PortType val);

    // --- Decode ---------------------------------------------------------------

    /**
     * @brief Port Type, upper word
     *
     * TRM name "PortType", bit 63: Port Type of SIDO2 (USB PD R3.3 Table 6.30).
     * Reset 1 (Guaranteed). Bit 31 (portTypeLower()) is the Port Type of SIDO1.
     * DPSPort's TRM rule ("If set, then PortType shall be 0") refers to this bit;
     * Table 6.30 extends it to both.
     */
    PortType portTypeUpper() const;

    /**
     * @brief Port Type, lower word
     *
     * TRM name "Port Type", bit 31: Port Type of SIDO1 (USB PD R3.3 Table 6.29).
     * Reset 1 (Guaranteed). Bit 63 (portTypeUpper()) is the Port Type of SIDO2.
     */
    PortType portTypeLower() const;

    /**
     * @brief DPS Port
     *
     * TRM name "DPSPort", bit 62. If set, PortType (bit 63) shall be 0 (Managed
     * Capability Port); isSemanticallyValid() enforces that.
     */
    bool dpsPort() const;

    /// @brief Raw Port Maximum PDP in 0.5 W steps (TRM "PortMaximumPDP_0p5W", bits 49:41)
    uint16_t rawPortMaximumPdp0p5W() const;
    /// @brief Port Maximum PDP in mW (bits 49:41, raw x 500). Up to 255 500 mW, so uint32_t.
    uint32_t portMaximumPdp0p5W_mW() const;

    /// @brief Raw Port Guaranteed PDP in 0.5 W steps (TRM "PortGuaranteedPDP_0p5W", bits 40:32)
    uint16_t rawPortGuaranteedPdp0p5W() const;
    /// @brief Port Guaranteed PDP in mW (bits 40:32, raw x 500). Up to 255 500 mW, so uint32_t.
    uint32_t portGuaranteedPdp0p5W_mW() const;

    /// @brief Raw Port Maximum PDP in 1 W steps (TRM "Port Maximum PDP", bits 23:16)
    uint8_t rawPortMaximumPdp() const;
    /// @brief Port Maximum PDP in mW (bits 23:16, raw x 1000)
    uint32_t portMaximumPdp_mW() const;

    /// @brief Raw Port Present PDP in 1 W steps (TRM "Port Present PDP", bits 15:8)
    uint8_t rawPortPresentPdp() const;
    /// @brief Port Present PDP in mW (bits 15:8, raw x 1000)
    uint32_t portPresentPdp_mW() const;

    /// @brief Raw Port Reported PDP in 1 W steps (TRM "Port Reported PDP", bits 7:0)
    uint8_t rawPortReportedPdp() const;
    /// @brief Port Reported PDP in mW (bits 7:0, raw x 1000)
    uint32_t portReportedPdp_mW() const;

    // --- Setters: each a single setBits() call (setBits() itself guards !isValid()) ---

    /// @brief Set Port Type, upper word (bit 63); see portTypeUpper()
    void setPortTypeUpper(PortType type);
    /// @brief Set Port Type, lower word (bit 31); see portTypeLower()
    void setPortTypeLower(PortType type);
    /// @brief Set DPS Port (bit 62); see dpsPort()
    void setDpsPort(bool enabled);

    /// @brief Set raw Port Maximum PDP, 0.5 W steps (bits 49:41); masked to 9 bits, so it cannot reach bit 50
    void setRawPortMaximumPdp0p5W(uint16_t raw);
    /// @brief Set raw Port Guaranteed PDP, 0.5 W steps (bits 40:32); masked to 9 bits, so it cannot reach bit 41
    void setRawPortGuaranteedPdp0p5W(uint16_t raw);
    /// @brief Set raw Port Maximum PDP, 1 W steps (bits 23:16)
    void setRawPortMaximumPdp(uint8_t raw);
    /// @brief Set raw Port Present PDP, 1 W steps (bits 15:8)
    void setRawPortPresentPdp(uint8_t raw);
    /// @brief Set raw Port Reported PDP, 1 W steps (bits 7:0)
    void setRawPortReportedPdp(uint8_t raw);

    /**
     * @name Unit setters
     * Each stores raw = mW / LSB, rounded down (floor, as the library's PDO
     * builders do), and clamps the result to the field maximum (511 for the 9-bit
     * fields, 255 for the 8-bit fields) before writing it. An over-range input
     * saturates; it never wraps. For example 300 000 mW on a 0.5 W field stores 511
     * (255 500 mW), not 600 & 0x1FF.
     * @{
     */
    void setPortMaximumPdp0p5W_mW(uint32_t mW);
    void setPortGuaranteedPdp0p5W_mW(uint32_t mW);
    void setPortMaximumPdp_mW(uint32_t mW);
    void setPortPresentPdp_mW(uint32_t mW);
    void setPortReportedPdp_mW(uint32_t mW);
    /** @} */

    /**
     * @brief Print the raw value and every field with its raw code and power
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus:
     * - reserved bits 61:50 and 30:24 read 0 (TRM)
     * - DPSPort set requires Port Type (bit 63) to be Managed (TRM: "If set, then
     *   PortType shall be 0"); bit 31 is not checked
     * - Port Guaranteed PDP (0.5 W) is at most Port Maximum PDP (0.5 W), skipped
     *   when the maximum is 0. This rule is inferred from the TRM field
     *   descriptions ("Minimum power the port is guaranteed to always be able to
     *   provide" against "Maximum power the port will provide"); the TRM does not
     *   state it.
     */
    bool isSemanticallyValid() const override;

    // --- Bit positions and masks (SLVUCR8B Rev. B, p.55) ----------------------

    static constexpr uint8_t kPortTypeUpperBit = 63;
    static constexpr uint8_t kDpsPortBit = 62;
    static constexpr uint8_t kPortMaximumPdp0p5WBit = 41;      ///< bits 49:41, 9 bits
    static constexpr uint8_t kPortGuaranteedPdp0p5WBit = 32;   ///< bits 40:32, 9 bits
    static constexpr uint8_t kPortTypeLowerBit = 31;
    static constexpr uint8_t kPortMaximumPdpBit = 16;          ///< bits 23:16, 8 bits
    static constexpr uint8_t kPortPresentPdpBit = 8;           ///< bits 15:8, 8 bits
    static constexpr uint8_t kPortReportedPdpBit = 0;          ///< bits 7:0, 8 bits

    /**
     * Reserved bits 30:24, as a mask on the lower 32-bit word (bits 31:0 of the
     * register): seven bits, 0x7F, shifted to bit 24 = 0x7F000000.
     */
    static constexpr uint32_t kReservedMaskLower = 0x7F000000u;

    /**
     * Reserved bits 61:50, as a mask on the upper 32-bit word (bits 63:32 of the
     * register). Register bit 50 is upper-word bit 18 and register bit 61 is
     * upper-word bit 29, so the twelve bits 29:18 are 0xFFF << 18 = 0x3FFC0000.
     */
    static constexpr uint32_t kReservedMaskUpper = 0x3FFC0000u;
};
