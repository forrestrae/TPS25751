#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief Minimum time the PD controller waits before entering sleep mode
 *
 * @see TI TPS25751 Technical Reference Manual, Table 3-27 (bits 2:1, Sleep Time)
 */
enum class SleepTime : uint8_t {
    Reserved0 = 0,   ///< 0h - Reserved
    Time100ms = 1,   ///< 1h - 100 ms
    Time1200ms = 2,  ///< 2h - 1200 ms
    Reserved3 = 3    ///< 3h - Reserved
};

/**
 * @brief TPS25751 Sleep Control Register (0x70)
 *
 * Controls whether, and how quickly, the PD controller is permitted to enter
 * sleep modes after the device has been idle. This is a read/write register
 * that this batch introduces the library's first write path for.
 *
 * Register Details:
 * - Address: 0x70
 * - Size: 1 byte
 * - Access: Read/Write
 * - Reset value: 0x01 (Sleep Mode Allowed set, Sleep Time = 0h/Reserved) --
 *   note this reset state is itself semantically INVALID per this class's
 *   isSemanticallyValid() (sleep allowed with a reserved sleep time), which
 *   callers should be prepared for on a factory-fresh device.
 *
 * @see TI TPS25751 Technical Reference Manual, Section 3.2.28, Table 3-27
 */
class TPS25751SleepControl : public TPS25751Register
{
public:
    explicit TPS25751SleepControl() : TPS25751Register(TPS25751Registers::Registers::SLEEP_CONTROL.size) {}
    explicit TPS25751SleepControl(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::SLEEP_CONTROL.size) {}
    TPS25751SleepControl(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751SleepControl(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::SLEEP_CONTROL;

    /**
     * @brief Get the Sleep Mode Allowed bit
     *
     * If asserted, the PD controller will enter sleep modes after the device
     * is idle for sleepTime().
     *
     * @return true if sleep mode is allowed, false otherwise
     * @see TI TPS25751 Technical Reference Manual, Table 3-27, bit 0
     */
    bool sleepModeAllowed() const;

    /**
     * @brief Get the Sleep Time field
     *
     * The minimum time the PD controller waits before entering sleep mode.
     *
     * @return The configured SleepTime value
     * @see TI TPS25751 Technical Reference Manual, Table 3-27, bits 2:1
     */
    SleepTime sleepTime() const;

    /**
     * @brief Set the Sleep Mode Allowed bit (read-modify-write of bit 0 only)
     * @param allowed true to permit the PD controller to enter sleep modes
     */
    void setSleepModeAllowed(bool allowed);

    /**
     * @brief Set the Sleep Time field (read-modify-write of bits 2:1 only)
     * @param t The SleepTime value to write
     */
    void setSleepTime(SleepTime t);

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates sleep control register data semantically
     *
     * Flags the inter-field inconsistency of Sleep Mode Allowed being set
     * while Sleep Time holds a Reserved code (0h or 3h). A Reserved Sleep
     * Time with sleep disallowed is acceptable, since the field is unused
     * in that case.
     *
     * @return true if the register is valid, correctly sized, and logically consistent
     */
    bool isSemanticallyValid() const override;

private:
    static constexpr uint8_t kSleepModeAllowedBit = 0;  ///< bit 0
    static constexpr uint8_t kSleepTimeShift = 1;       ///< bits 2:1 start at bit 1
    static constexpr uint8_t kSleepTimeBits = 2;        ///< bits 2:1 is a 2-bit field
};

// String conversion helper
const char* toString(SleepTime val);
