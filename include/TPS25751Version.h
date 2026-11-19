#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751Version.h
 * @brief VERSION register (0x0F) class implementation
 *
 * VERSION reports the bootloader or application code version of the controller,
 * written VVVV.MM.RR. It is read-only: there are no setters, no write path, and
 * no kAddress.
 *
 * Register Details:
 * - Address: 0x0F
 * - Size: 4 bytes (32 bits)
 * - Access: Read-only
 * - Reset: 00000000h
 *
 * Byte layout: the TRM says the value is returned little-endian, "byte 1 = RR,
 * byte 2 = MM, etc." The TRM numbers bytes from 1 and does not spell out the
 * VVVV bytes, so this class decodes RR from bits 7:0, MM from bits 15:8 and VVVV
 * from bits 31:16. Placing VVVV in bytes 3 and 4 is an inference from that
 * sentence and is still to be confirmed on hardware.
 *
 * The TRM does not say whether the digits are BCD, so the version is printed as
 * hexadecimal digits and the raw 32-bit value is available unmodified.
 *
 * @code
 * TPS25751 tps;
 * auto version = tps.readVersionRegister();
 * if (version) {
 *     version->debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), Section 3.2.5, Table 3-7, p.18
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751Version : public TPS25751Register
{
public:
    explicit TPS25751Version() : TPS25751Register(TPS25751Registers::Registers::VERSION.size) {}
    explicit TPS25751Version(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::VERSION.size) {}
    TPS25751Version(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751Version(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get the whole Version field
     * @return Raw 32-bit value, bits 31:0 (VVVV.MM.RR packed little-endian)
     * @see TI TPS25751 Technical Reference Manual, Table 3-7, bits 31:0
     */
    uint32_t rawVersion() const;

    /**
     * @brief Get the VVVV (major) part of the version
     *
     * Decoded from bits 31:16. The TRM only states "byte 1 = RR, byte 2 = MM,
     * etc.", so VVVV in bytes 3 and 4 is an inference, to be confirmed on
     * hardware.
     * @return 16-bit major version
     */
    uint16_t versionMajor() const;

    /**
     * @brief Get the MM (minor) part of the version
     * @return 8-bit minor version, bits 15:8 (byte 2 in the TRM's numbering)
     */
    uint8_t versionMinor() const;

    /**
     * @brief Get the RR (revision) part of the version
     * @return 8-bit revision, bits 7:0 (byte 1 in the TRM's numbering)
     */
    uint8_t versionRevision() const;

    /**
     * @brief Print the version as hex VVVV.MM.RR plus the raw 32-bit value
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation (no field rules)
     *
     * Chains the base checks only: valid buffer, not the all-0xFF bus-error
     * signature, and the expected size. 00000000h (the reset value) is valid.
     */
    bool isSemanticallyValid() const override;
};
