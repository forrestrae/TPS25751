#include <Stream.h>

#include "TPS25751TransmitBatteryCapabilities.h"

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

void printHex16Field(Stream &s, const __FlashStringHelper *label, uint16_t value)
{
    s.print(label);
    s.print(F("0x"));
    printHex16(s, value);
    s.print(F(" ("));
    s.print(value);
    s.print(F(")"));
}

}  // namespace

// --- Byte-assembly helpers --------------------------------------------------------
// Battery 3 sits past bit 255, so extractBits*() and setBits() (uint8_t bit
// position) cannot reach it. Every field of every entry goes through these.
// Bounds are written as byteOffset + 1 < _len in size_t so no arithmetic wraps and
// a 16-bit access never touches _raw[_len].

uint16_t TPS25751TransmitBatteryCapabilities::readLE16(size_t byteOffset) const
{
    if (!isValid() || byteOffset + 1 >= _len) return 0;

    return static_cast<uint16_t>(_raw[byteOffset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(_raw[byteOffset + 1]) << 8);
}

uint8_t TPS25751TransmitBatteryCapabilities::readByte(size_t byteOffset) const
{
    if (!isValid() || byteOffset >= _len) return 0;

    return _raw[byteOffset];
}

void TPS25751TransmitBatteryCapabilities::writeLE16(size_t byteOffset, uint16_t value)
{
    if (!isValid() || byteOffset + 1 >= _len) return;

    _raw[byteOffset] = static_cast<uint8_t>(value & 0xFFu);
    _raw[byteOffset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void TPS25751TransmitBatteryCapabilities::writeByte(size_t byteOffset, uint8_t value)
{
    if (!isValid() || byteOffset >= _len) return;

    _raw[byteOffset] = value;
}

// --- Decode ---------------------------------------------------------------------

uint16_t TPS25751TransmitBatteryCapabilities::vid(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return readLE16(entryOffset(battery) + kVidOffset);
}

uint16_t TPS25751TransmitBatteryCapabilities::pid(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return readLE16(entryOffset(battery) + kPidOffset);
}

uint16_t TPS25751TransmitBatteryCapabilities::batteryDesignCapacity(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return readLE16(entryOffset(battery) + kDesignCapacityOffset);
}

uint16_t TPS25751TransmitBatteryCapabilities::batteryLastFullChargeCapacity(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return readLE16(entryOffset(battery) + kLastFullChargeCapacityOffset);
}

uint8_t TPS25751TransmitBatteryCapabilities::batteryType(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return readByte(entryOffset(battery) + kBatteryTypeOffset);
}

// --- Typed decode (USB PD R3.3 Table 6.64), built on the accessors above ------------

bool TPS25751TransmitBatteryCapabilities::invalidBatteryReference(uint8_t battery) const
{
    return (batteryType(battery) & 0x01u) != 0;
}

bool TPS25751TransmitBatteryCapabilities::batteryPresent(uint8_t battery) const
{
    return batteryDesignCapacity(battery) != 0x0000u;
}

bool TPS25751TransmitBatteryCapabilities::designCapacityKnown(uint8_t battery) const
{
    const uint16_t raw = batteryDesignCapacity(battery);
    return raw != 0x0000u && raw != 0xFFFFu;
}

uint32_t TPS25751TransmitBatteryCapabilities::batteryDesignCapacity_mWh(uint8_t battery) const
{
    if (!designCapacityKnown(battery)) return 0;
    return static_cast<uint32_t>(batteryDesignCapacity(battery)) * 100u;  // 0.1 Wh = 100 mWh per step
}

bool TPS25751TransmitBatteryCapabilities::lastFullChargeCapacityKnown(uint8_t battery) const
{
    const uint16_t raw = batteryLastFullChargeCapacity(battery);
    return raw != 0x0000u && raw != 0xFFFFu;
}

uint32_t TPS25751TransmitBatteryCapabilities::batteryLastFullChargeCapacity_mWh(uint8_t battery) const
{
    if (!lastFullChargeCapacityKnown(battery)) return 0;
    return static_cast<uint32_t>(batteryLastFullChargeCapacity(battery)) * 100u;
}

// --- Setters ----------------------------------------------------------------------

void TPS25751TransmitBatteryCapabilities::setVid(uint8_t battery, uint16_t value)
{
    if (battery >= kBatteryCount) return;
    writeLE16(entryOffset(battery) + kVidOffset, value);
}

void TPS25751TransmitBatteryCapabilities::setPid(uint8_t battery, uint16_t value)
{
    if (battery >= kBatteryCount) return;
    writeLE16(entryOffset(battery) + kPidOffset, value);
}

void TPS25751TransmitBatteryCapabilities::setBatteryDesignCapacity(uint8_t battery, uint16_t value)
{
    if (battery >= kBatteryCount) return;
    writeLE16(entryOffset(battery) + kDesignCapacityOffset, value);
}

void TPS25751TransmitBatteryCapabilities::setBatteryLastFullChargeCapacity(uint8_t battery, uint16_t value)
{
    if (battery >= kBatteryCount) return;
    writeLE16(entryOffset(battery) + kLastFullChargeCapacityOffset, value);
}

void TPS25751TransmitBatteryCapabilities::setBatteryType(uint8_t battery, uint8_t value)
{
    if (battery >= kBatteryCount) return;
    writeByte(entryOffset(battery) + kBatteryTypeOffset, value);
}

// --- Debug ------------------------------------------------------------------------

void TPS25751TransmitBatteryCapabilities::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("TX_BATTERY_CAPABILITIES (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::TRANSMIT_BATTERY_CAPABILITIES), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    // One line per 9-byte entry so each line is one battery.
    s.println(F("Raw bytes (byte 0 first):"));
    for (size_t start = 0; start < size(); start += kEntrySize)
    {
        size_t end = start + kEntrySize;
        if (end > size()) end = size();

        s.print(F("  bytes "));
        s.print(static_cast<unsigned long>(start));
        s.print(F("-"));
        s.print(static_cast<unsigned long>(end - 1));
        s.print(F(":"));
        for (size_t i = start; i < end; i++)
        {
            s.print(F(" "));
            printHex8(s, raw()[i]);
        }
        s.println();
    }

    for (uint8_t i = 0; i < kBatteryCount; i++)
    {
        s.print(F("Battery "));
        s.print(i);
        s.print(F(": VID 0x"));
        printHex16(s, vid(i));
        s.print(F(", PID 0x"));
        printHex16(s, pid(i));
        printHex16Field(s, F(", Design Capacity "), batteryDesignCapacity(i));
        printHex16Field(s, F(", Last Full Charge Capacity "), batteryLastFullChargeCapacity(i));
        s.print(F(", Battery Type 0x"));
        printHex8(s, batteryType(i));
        s.println();
    }

    // Typed decode, USB PD R3.3 Table 6.64.
    for (uint8_t i = 0; i < kBatteryCount; i++)
    {
        s.print(F("Battery "));
        s.print(i);
        s.print(F(": "));
        if (!batteryPresent(i))
        {
            s.print(F("not present"));
        }
        else
        {
            s.print(F("present, design capacity "));
            if (designCapacityKnown(i))
            {
                s.print(static_cast<unsigned long>(batteryDesignCapacity_mWh(i)));
                s.print(F(" mWh"));
            }
            else
            {
                s.print(F("unknown"));
            }
            s.print(F(", last full charge capacity "));
            if (lastFullChargeCapacityKnown(i))
            {
                s.print(static_cast<unsigned long>(batteryLastFullChargeCapacity_mWh(i)));
                s.print(F(" mWh"));
            }
            else
            {
                s.print(F("unknown"));
            }
        }
        if (invalidBatteryReference(i)) s.print(F(", invalid battery reference"));
        s.println();
    }

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751TransmitBatteryCapabilities::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_BATTERY_CAPABILITIES.size;
    if (!isValidSize(expectedSize)) return false;

    // No reserved bits and no field rule: the TRM states none. The agreement of
    // the entries with the battery counts in register 0x77 cannot be checked from
    // this register alone.
    return true;
}
