#include "TPS25751GPIOStatus.h"

bool TPS25751GPIOStatus::gpioDirection(uint8_t pin) const {
    if (pin > 7) return false;  // Only GPIO 0-7 have direction bits
    return extractBits(32 + pin, 1);
}

bool TPS25751GPIOStatus::gpioData(uint8_t pin) const {
    if (pin == 12) {
        return extractBits(12, 1);
    }
    if (pin > 7) return false;  // Only GPIO 0-7 and 12 have data bits
    return extractBits(pin, 1);
}

bool TPS25751GPIOStatus::gpio12Data() const {
    return extractBits(12, 1);
}

void TPS25751GPIOStatus::debugPrint(Stream &s) const {
    s.println(F("=== GPIO Status Register (0x72) ==="));

    s.println(F("  GPIO Directions (0=Input, 1=Output):"));
    for (uint8_t i = 0; i <= 7; i++) {
        s.print(F("    GPIO "));
        s.print(i);
        s.print(F(": "));
        s.println(gpioDirection(i) ? F("Output") : F("Input"));
    }

    s.println(F("  GPIO Data (Pin States):"));
    for (uint8_t i = 0; i <= 7; i++) {
        s.print(F("    GPIO "));
        s.print(i);
        s.print(F(": "));
        s.println(gpioData(i) ? F("HIGH") : F("LOW"));
    }

    s.print(F("   GPIO 12: "));
    s.println(gpio12Data() ? F("HIGH") : F("LOW"));

    s.println();
}

bool TPS25751GPIOStatus::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::GPIO_STATUS.size;
    if (!isValidSize(expectedSize)) return false;

    // GPIO status is primarily hardware state reporting
    // No specific semantic validation rules beyond basic checks
    // All bit patterns are valid hardware states

    return true;
}
