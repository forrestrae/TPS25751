#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Customer Use Register (0x06)
 *
 * These 8 bytes are allocated for customer use as needed. The PD controller
 * does not use this register itself. The register has no internal field
 * structure: it is an opaque, application-defined 64-bit blob (byte 0 = LSB).
 *
 * @note Read-only. SLVUCR8B Rev. B (p.15) marks the field [R]: it "can only be
 * changed during application customization, not at runtime." On hardware the
 * controller ACKs a runtime write and keeps the old value (bench, 2026-11-19).
 * The class therefore has no setters and no `kAddress`, so
 * TPS25751::writeRegister<T>() does not accept it; set the bytes in the
 * application configuration instead (see CONSTRAINTS.md, Read/Write Registers).
 *
 * @note The register's reset state is all zeros. The base class's
 * hasValidData() heuristic only flags an all-`0xFF` buffer as invalid (the
 * bus-error signature) -- an all-zero buffer, such as this register's reset
 * value, passes hasValidData(). Because CUSTOMER_USE has no field structure
 * to validate, isSemanticallyValid() here does not call hasValidData() at
 * all (mirroring TPS25751Data); callers should rely on isValid() /
 * isSemanticallyValid() rather than hasValidData() for this register.
 *
 * Register Details:
 * - Address: 0x06
 * - Size: 8 bytes
 * - Access: Read-only (set during application customization)
 * - Reset value: 0x00 * 8
 *
 * @see TI TPS25751 Technical Reference Manual, Section 3.2.2, Table 3-4
 */
class TPS25751CustomerUse : public TPS25751Register
{
public:
    explicit TPS25751CustomerUse() : TPS25751Register(TPS25751Registers::Registers::CUSTOMER_USE.size) {}
    explicit TPS25751CustomerUse(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::CUSTOMER_USE.size) {}
    TPS25751CustomerUse(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751CustomerUse(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get the full 8-byte value assembled little-endian (byte 0 = LSB)
     * @return The 64-bit value, or 0 if the register is invalid
     */
    uint64_t value() const;

    /**
     * @brief Get a single raw byte from the register
     * @param index Byte offset (0-based)
     * @return The byte at @p index, or 0 if out of range / invalid
     */
    uint8_t byteAt(size_t index) const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates the customer-use register semantically
     *
     * CUSTOMER_USE has no fixed field layout, so semantic validation is
     * limited to the generic checks the base class already performs
     * (non-null, correctly sized buffer). Any byte pattern is semantically
     * valid.
     *
     * @return true if the register has valid, correctly-sized backing storage
     */
    bool isSemanticallyValid() const override;
};
