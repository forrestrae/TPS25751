#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 GPIO Status Register (0x72)
 *
 * This register captures the status and settings of all GPIO pins.
 * The available GPIO pins may vary by device type - check the device-specific datasheet.
 *
 * Register Size: 8 bytes
 * Access: Read-only
 * Address: 0x72
 */
class TPS25751GPIOStatus : public TPS25751Register
{
public:
    explicit TPS25751GPIOStatus() : TPS25751Register(TPS25751Registers::Registers::GPIO_STATUS.size) {}
    explicit TPS25751GPIOStatus(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::GPIO_STATUS.size) {}
    TPS25751GPIOStatus(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751GPIOStatus(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get direction of GPIO pin (input or output)
     * @param pin GPIO pin number (0-7)
     * @return true if configured as output, false if input
     */
    bool gpioDirection(uint8_t pin) const;

    /**
     * @brief Get data value of GPIO pin
     * @param pin GPIO pin number (0-7, or 12)
     * @return true if logic high detected, false if logic low
     */
    bool gpioData(uint8_t pin) const;

    /**
     * @brief Check if GPIO 12 data is high
     * @return true if logic high on GPIO 12
     */
    bool gpio12Data() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates GPIO status register data semantically
     * @return true if GPIO status data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
