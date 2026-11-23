#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751IOConfig.h
 * @brief IO_CONFIG register (0x5C) class implementation
 *
 * IO_CONFIG holds the application-specific GPIO configuration (output/input
 * enables, pull resistors, drive strength, event mapping, analog-input
 * selection). Per the TRM: **"This register cannot be modified at run-time,
 * the GPIO configurations are set according to the configuration during the
 * boot process."** Every field is documented [R] in the TRM/register-docs, so
 * this class is decode-only -- there are no setters, no write path, and no
 * `kAddress` (which exists only to support the typed write path).
 *
 * Register Details:
 * - Address: 0x5C
 * - Size: 49 bytes (392 bits)
 * - Access: Read-only
 *
 * All fields are byte-aligned. Bit offsets in the TRM run past 255 (e.g. the
 * GPIO12 Mapped Event field starts at bit 384). `TPS25751Register::extractBits*()`
 * takes a 16-bit bit position (ADR-012 in ARCHITECTURE.md), so every field is
 * read through it at bit (byte offset * 8). Before ADR-012 the position was
 * 8 bits and this class assembled its fields from `_raw[]` by hand.
 *
 * 13-bit-per-GPIO bitmaps (bit *n* of the mask corresponds to GPIO *n*, for
 * GPIO0..GPIO12):
 * | Field | Byte(s) | Meaning |
 * |---|---|---|
 * | GPIO Output Enable | 0-1 | 1 = Output Enabled, 0 = Hi-Z |
 * | GPIO Interrupt Enable | 4-5 | 1 = Interrupt Enabled |
 * | Initial Value | 8-9 | Default output level (1 = drive high) |
 * | Open Drain Output Enable | 12-13 | 0 = push-pull, 1 = open-drain |
 * | Internal Pull Down Enable | 20-21 | 1 = enabled |
 * | Internal Pull Up Enable | 24-25 | 1 = enabled |
 * | GPIO Event Polarity | 32-33 | Inverts the polarity of the mapped output event |
 *
 * Byte 28 holds four independent analog-input-enable bits (note: the TRM's
 * own field descriptions for "GPIO AI Enable GPIO 0/2" both read "Assert when
 * GPIO4 is used as an analog input" -- a known copy-paste artifact in the
 * datasheet; the accessors below are named after the FIELD, not that
 * erroneous description text).
 *
 * The "Mapped Event" fields are one whole byte per GPIO (GPIO0-GPIO7 at bytes
 * 36-43, GPIO10-GPIO12 at bytes 46-48); GPIO8/GPIO9 (bytes 44-45) are reserved
 * and have no accessor.
 *
 * @code
 * TPS25751 tps;
 * auto ioConfig = tps.readIOConfigRegister();
 * if (ioConfig) {
 *     ioConfig->debugPrint();
 *     if (ioConfig->outputEnabled(4)) {
 *         Serial.println("GPIO4 configured as output");
 *     }
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), Section 3.2.26, Table 3-28, p.48
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751IOConfig : public TPS25751Register
{
public:
    explicit TPS25751IOConfig() : TPS25751Register(TPS25751Registers::Registers::IO_CONFIG.size) {}
    explicit TPS25751IOConfig(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::IO_CONFIG.size) {}
    TPS25751IOConfig(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751IOConfig(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get the raw 13-bit GPIO Output Enable bitmap
     * @return Bit *n* set means GPIO *n* is configured as an output (1 = Output Enabled, 0 = Hi-Z)
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 12:0
     */
    uint16_t gpioOutputEnableMask() const;

    /**
     * @brief Check whether a given GPIO is configured as an output
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Output Enable bit is set
     */
    bool outputEnabled(uint8_t gpio) const;

    /**
     * @brief Get the raw 13-bit GPIO Interrupt Enable bitmap
     * @return Bit *n* set means GPIO *n* has its interrupt enabled
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 44:32
     */
    uint16_t gpioInterruptEnableMask() const;

    /**
     * @brief Check whether a given GPIO has its interrupt enabled
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Interrupt Enable bit is set
     */
    bool interruptEnabled(uint8_t gpio) const;

    /**
     * @brief Get the raw 13-bit Initial Value bitmap
     * @return Bit *n* set means GPIO *n*'s default output level is high
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 76:64
     */
    uint16_t gpioInitialValueMask() const;

    /**
     * @brief Check whether a given GPIO's default output level is high
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Initial Value bit is set (drive high)
     */
    bool initialValueHigh(uint8_t gpio) const;

    /**
     * @brief Get the raw 13-bit Open Drain Output Enable bitmap
     * @return Bit *n* set means GPIO *n* is open-drain (0 = push-pull)
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 108:96
     */
    uint16_t gpioOpenDrainEnableMask() const;

    /**
     * @brief Check whether a given GPIO's output is open-drain
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Open Drain Output Enable bit is set
     */
    bool openDrainEnabled(uint8_t gpio) const;

    /**
     * @brief Get the raw 13-bit Internal Pull Down Enable bitmap
     * @return Bit *n* set means GPIO *n*'s internal pull-down is enabled
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 172:160
     */
    uint16_t gpioPullDownEnableMask() const;

    /**
     * @brief Check whether a given GPIO's internal pull-down is enabled
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Internal Pull Down Enable bit is set
     */
    bool pullDownEnabled(uint8_t gpio) const;

    /**
     * @brief Get the raw 13-bit Internal Pull Up Enable bitmap
     * @return Bit *n* set means GPIO *n*'s internal pull-up is enabled
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 204:192
     */
    uint16_t gpioPullUpEnableMask() const;

    /**
     * @brief Check whether a given GPIO's internal pull-up is enabled
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Internal Pull Up Enable bit is set
     */
    bool pullUpEnabled(uint8_t gpio) const;

    /**
     * @brief Get the raw 13-bit GPIO Event Polarity bitmap
     * @return Bit *n* set means the output event mapped to GPIO *n* has inverted polarity
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bits 268:256
     */
    uint16_t gpioEventPolarityMask() const;

    /**
     * @brief Check whether a given GPIO's mapped output event has inverted polarity
     * @param gpio GPIO index (0-12); out-of-range returns false
     * @return true if the GPIO's Event Polarity bit is set
     */
    bool eventPolarityInverted(uint8_t gpio) const;

    /**
     * @brief Get the GPIO AI Enable GPIO 0 bit (byte 28, bit 0)
     * @return true if analog-input mode is enabled for GPIO0
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bit 224
     */
    bool gpio0AnalogInputEnabled() const;

    /**
     * @brief Get the GPIO AI Enable GPIO 2 bit (byte 28, bit 2)
     * @return true if analog-input mode is enabled for GPIO2
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bit 226
     */
    bool gpio2AnalogInputEnabled() const;

    /**
     * @brief Get the GPIO 4 Analog Input Control bit (byte 28, bit 4)
     * @return true if GPIO4 is used as an analog input (must also be set
     *         when PORT_CONTROL.ChargerDetectEnable/ChargerAdvertiseEnable is non-zero)
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bit 228
     */
    bool gpio4AnalogInputControl() const;

    /**
     * @brief Get the GPIO 5 Analog Input Control bit (byte 28, bit 5)
     * @return true if GPIO5 is used as an analog input (must also be set
     *         when PORT_CONTROL.ChargerDetectEnable/ChargerAdvertiseEnable is non-zero)
     * @see TI TPS25751 Technical Reference Manual, Table 3-28, bit 229
     */
    bool gpio5AnalogInputControl() const;

    /**
     * @brief Get the Mapped Event byte for a given GPIO
     *
     * Valid for GPIO 0-7 and GPIO 10-12. GPIO8/GPIO9 are reserved (no mapped
     * event field exists for them) and out-of-range indices are likewise
     * unsupported.
     *
     * @param gpio GPIO index
     * @return The GPIO's raw Mapped Event byte, or 0 for GPIO8/GPIO9/out-of-range
     * @see TI TPS25751 Technical Reference Manual, Table 3-28 (GPIO Mapped Event fields)
     */
    uint8_t gpioMappedEvent(uint8_t gpio) const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates IO_CONFIG register data semantically
     *
     * Per-pin configuration is independent -- there are no cross-field
     * consistency rules to enforce. An all-zero buffer is not rejected (it
     * is a legitimate configuration, e.g. all GPIOs left at Hi-Z/disabled).
     *
     * @return true if the register is valid, correctly sized, and not the
     *         all-0xFF bus-error signature
     */
    bool isSemanticallyValid() const override;

private:
    // Byte offsets of the 13-bit-per-GPIO bitmap fields (little-endian pairs).
    static constexpr size_t kOutputEnableByte = 0;
    static constexpr size_t kInterruptEnableByte = 4;
    static constexpr size_t kInitialValueByte = 8;
    static constexpr size_t kOpenDrainEnableByte = 12;
    static constexpr size_t kPullDownEnableByte = 20;
    static constexpr size_t kPullUpEnableByte = 24;
    static constexpr size_t kEventPolarityByte = 32;

    static constexpr size_t kAnalogInputByte = 28;
    static constexpr uint8_t kGpio0AiBit = 0;
    static constexpr uint8_t kGpio2AiBit = 2;
    static constexpr uint8_t kGpio4AiBit = 4;
    static constexpr uint8_t kGpio5AiBit = 5;

    static constexpr uint8_t kGpioBitmapBits = 13;  // GPIO0..GPIO12
    static constexpr uint8_t kMaxGpioIndex = 12;

    /**
     * @brief Read a little-endian 13-bit GPIO bitmap starting at @p byteOffset
     * @param byteOffset Offset of the low byte of the 2-byte pair
     * @return The masked 13-bit value, or 0 if out of bounds / invalid
     */
    uint16_t readGpioBitmap(size_t byteOffset) const;

    /**
     * @brief Extract bit @p gpio from a 13-bit bitmap, bounds-checked
     * @param mask The bitmap (as returned by readGpioBitmap())
     * @param gpio GPIO index (0-12); out-of-range returns false
     */
    static bool bitmapBit(uint16_t mask, uint8_t gpio);
};
