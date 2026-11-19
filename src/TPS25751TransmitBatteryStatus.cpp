#include <Stream.h>

#include "TPS25751TransmitBatteryStatus.h"

namespace {

void printHex8(Stream &s, uint8_t value)
{
    // Zero-padded to 2 hex digits; Stream's HEX output drops leading zeros.
    s.print(static_cast<unsigned int>((value >> 4) & 0xFu), HEX);
    s.print(static_cast<unsigned int>(value & 0xFu), HEX);
}

void printHex16(Stream &s, uint16_t value)
{
    printHex8(s, static_cast<uint8_t>(value >> 8));
    printHex8(s, static_cast<uint8_t>(value & 0xFFu));
}

}  // namespace

// --- Decode ---------------------------------------------------------------------

uint8_t TPS25751TransmitBatteryStatus::batteryInfo(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return extractBits(static_cast<uint8_t>(32 * battery + 8), 8);
}

uint16_t TPS25751TransmitBatteryStatus::rawPresentCapacity(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return extractBits16(static_cast<uint8_t>(32 * battery + 16), 16);
}

// --- Setters: each a single setBits() call after the index guard -----------------

void TPS25751TransmitBatteryStatus::setBatteryInfo(uint8_t battery, uint8_t value)
{
    if (!isValid() || battery >= kBatteryCount) return;
    setBits(static_cast<uint8_t>(32 * battery + 8), 8, value);
}

void TPS25751TransmitBatteryStatus::setRawPresentCapacity(uint8_t battery, uint16_t value)
{
    if (!isValid() || battery >= kBatteryCount) return;
    setBits(static_cast<uint8_t>(32 * battery + 16), 16, value);
}

// --- Debug ------------------------------------------------------------------------

void TPS25751TransmitBatteryStatus::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("TRANSMITTED_BATTERY_STATUS (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::TRANSMIT_BATTERY_STATUS), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("Raw bytes (byte 0 first):"));
    for (size_t i = 0; i < size(); i++)
    {
        s.print(F(" "));
        printHex8(s, raw()[i]);
    }
    s.println();

    for (uint8_t i = 0; i < kBatteryCount; i++)
    {
        const uint16_t capacity = rawPresentCapacity(i);
        s.print(F("Battery "));
        s.print(i);
        s.print(F(": Battery Info 0x"));
        printHex8(s, batteryInfo(i));
        s.print(F(", Present Capacity 0x"));
        printHex16(s, capacity);
        s.print(F(" ("));
        s.print(capacity);
        s.println(F(")"));
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751TransmitBatteryStatus::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_BATTERY_STATUS.size;
    if (!isValidSize(expectedSize)) return false;

    // The low byte of every 4-byte entry is reserved (bits 7:0, 39:32, 71:64,
    // 103:96) and reads 0. The TRM states no other rule.
    for (uint8_t i = 0; i < kBatteryCount; i++)
    {
        const size_t byteIndex = static_cast<size_t>(kEntrySize) * i;
        const uint8_t reserved = raw()[byteIndex];
        if (reserved != 0)
        {
            TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "TRANSMITTED_BATTERY_STATUS: reserved byte %u set (0x%02X)", static_cast<unsigned>(byteIndex), static_cast<unsigned>(reserved));
            return false;
        }
    }

    return true;
}
