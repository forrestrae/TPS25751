#include "TPS25751IOConfig.h"

uint16_t TPS25751IOConfig::readGpioBitmap(size_t byteOffset) const
{
    if (!isValid() || byteOffset + 1 >= _len) return 0;

    uint16_t raw = static_cast<uint16_t>(_raw[byteOffset]) |
                   (static_cast<uint16_t>(_raw[byteOffset + 1]) << 8);
    return raw & kGpioBitmapMask;
}

bool TPS25751IOConfig::bitmapBit(uint16_t mask, uint8_t gpio)
{
    if (gpio > kMaxGpioIndex) return false;
    return (mask >> gpio) & 0x1;
}

uint16_t TPS25751IOConfig::gpioOutputEnableMask() const
{
    return readGpioBitmap(kOutputEnableByte);
}

bool TPS25751IOConfig::outputEnabled(uint8_t gpio) const
{
    return bitmapBit(gpioOutputEnableMask(), gpio);
}

uint16_t TPS25751IOConfig::gpioInterruptEnableMask() const
{
    return readGpioBitmap(kInterruptEnableByte);
}

bool TPS25751IOConfig::interruptEnabled(uint8_t gpio) const
{
    return bitmapBit(gpioInterruptEnableMask(), gpio);
}

uint16_t TPS25751IOConfig::gpioInitialValueMask() const
{
    return readGpioBitmap(kInitialValueByte);
}

bool TPS25751IOConfig::initialValueHigh(uint8_t gpio) const
{
    return bitmapBit(gpioInitialValueMask(), gpio);
}

uint16_t TPS25751IOConfig::gpioOpenDrainEnableMask() const
{
    return readGpioBitmap(kOpenDrainEnableByte);
}

bool TPS25751IOConfig::openDrainEnabled(uint8_t gpio) const
{
    return bitmapBit(gpioOpenDrainEnableMask(), gpio);
}

uint16_t TPS25751IOConfig::gpioPullDownEnableMask() const
{
    return readGpioBitmap(kPullDownEnableByte);
}

bool TPS25751IOConfig::pullDownEnabled(uint8_t gpio) const
{
    return bitmapBit(gpioPullDownEnableMask(), gpio);
}

uint16_t TPS25751IOConfig::gpioPullUpEnableMask() const
{
    return readGpioBitmap(kPullUpEnableByte);
}

bool TPS25751IOConfig::pullUpEnabled(uint8_t gpio) const
{
    return bitmapBit(gpioPullUpEnableMask(), gpio);
}

uint16_t TPS25751IOConfig::gpioEventPolarityMask() const
{
    return readGpioBitmap(kEventPolarityByte);
}

bool TPS25751IOConfig::eventPolarityInverted(uint8_t gpio) const
{
    return bitmapBit(gpioEventPolarityMask(), gpio);
}

bool TPS25751IOConfig::gpio0AnalogInputEnabled() const
{
    if (!isValid() || kAnalogInputByte >= _len) return false;
    return (_raw[kAnalogInputByte] >> kGpio0AiBit) & 0x1;
}

bool TPS25751IOConfig::gpio2AnalogInputEnabled() const
{
    if (!isValid() || kAnalogInputByte >= _len) return false;
    return (_raw[kAnalogInputByte] >> kGpio2AiBit) & 0x1;
}

bool TPS25751IOConfig::gpio4AnalogInputControl() const
{
    if (!isValid() || kAnalogInputByte >= _len) return false;
    return (_raw[kAnalogInputByte] >> kGpio4AiBit) & 0x1;
}

bool TPS25751IOConfig::gpio5AnalogInputControl() const
{
    if (!isValid() || kAnalogInputByte >= _len) return false;
    return (_raw[kAnalogInputByte] >> kGpio5AiBit) & 0x1;
}

uint8_t TPS25751IOConfig::gpioMappedEvent(uint8_t gpio) const
{
    size_t byteOffset;
    switch (gpio) {
        case 0: byteOffset = 36; break;
        case 1: byteOffset = 37; break;
        case 2: byteOffset = 38; break;
        case 3: byteOffset = 39; break;
        case 4: byteOffset = 40; break;
        case 5: byteOffset = 41; break;
        case 6: byteOffset = 42; break;
        case 7: byteOffset = 43; break;
        // 8, 9 are reserved (bytes 44-45) -- no mapped-event field.
        case 10: byteOffset = 46; break;
        case 11: byteOffset = 47; break;
        case 12: byteOffset = 48; break;
        default: return 0;
    }

    if (!isValid() || byteOffset >= _len) return 0;
    return _raw[byteOffset];
}

void TPS25751IOConfig::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("IO_CONFIG (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::IO_CONFIG), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("GPIO Output Enable Mask: 0x"));
    s.println(gpioOutputEnableMask(), HEX);

    s.print(F("GPIO Interrupt Enable Mask: 0x"));
    s.println(gpioInterruptEnableMask(), HEX);

    s.print(F("GPIO Initial Value Mask: 0x"));
    s.println(gpioInitialValueMask(), HEX);

    s.print(F("GPIO Open Drain Enable Mask: 0x"));
    s.println(gpioOpenDrainEnableMask(), HEX);

    s.print(F("GPIO Pull Down Enable Mask: 0x"));
    s.println(gpioPullDownEnableMask(), HEX);

    s.print(F("GPIO Pull Up Enable Mask: 0x"));
    s.println(gpioPullUpEnableMask(), HEX);

    s.print(F("GPIO Event Polarity Mask: 0x"));
    s.println(gpioEventPolarityMask(), HEX);

    s.print(F("GPIO0 Analog Input Enabled: "));
    s.println(gpio0AnalogInputEnabled() ? F("Yes") : F("No"));

    s.print(F("GPIO2 Analog Input Enabled: "));
    s.println(gpio2AnalogInputEnabled() ? F("Yes") : F("No"));

    s.print(F("GPIO4 Analog Input Control: "));
    s.println(gpio4AnalogInputControl() ? F("Yes") : F("No"));

    s.print(F("GPIO5 Analog Input Control: "));
    s.println(gpio5AnalogInputControl() ? F("Yes") : F("No"));

    s.println(F("GPIO Mapped Events:"));
    for (uint8_t gpio = 0; gpio <= kMaxGpioIndex; gpio++) {
        if (gpio == 8 || gpio == 9) continue;  // reserved, no mapped-event field
        s.print(F("  GPIO"));
        s.print(gpio);
        s.print(F(": 0x"));
        s.println(gpioMappedEvent(gpio), HEX);
    }

    s.println(F("========================================"));
}

bool TPS25751IOConfig::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::IO_CONFIG.size;
    if (!isValidSize(expectedSize)) return false;

    // Per-pin configuration is independent; no cross-field consistency rules.
    return true;
}
