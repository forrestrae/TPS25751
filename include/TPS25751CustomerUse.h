#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Customer Use Register (0x06)
 *
 * These 8 bytes are allocated for customer use as needed. The PD controller
 * does not use this register itself; it may be freely read and written during
 * application customization. The register has no internal field structure --
 * it is an opaque, application-defined 64-bit blob (byte 0 = LSB).
 *
 * @note The TRM's field table marks the Customer Use field as [R], but the
 * register itself is documented (Table 3-4) as Read/Write and may be changed
 * by the application; `RegisterInfo` and this class treat it as R/W.
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
 * - Access: Read/Write
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

    /**
     * @brief Set the full 8-byte value (little-endian, inverse of value())
     * @param v The 64-bit value to write
     */
    void setValue(uint64_t v);

    /**
     * @brief Copy raw bytes into the register starting at offset 0
     * @param src Pointer to the source bytes (no-op if nullptr)
     * @param len Number of bytes to copy; copies min(len, 8)
     */
    void setBytes(const uint8_t* src, size_t len);

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
