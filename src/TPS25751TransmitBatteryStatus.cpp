#include <Stream.h>

#include "TPS25751TransmitBatteryStatus.h"

// --- Decode ---------------------------------------------------------------------

uint8_t TPS25751TransmitBatteryStatus::batteryInfo(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return extractBits(batteryInfoBit(battery), 8);
}

uint16_t TPS25751TransmitBatteryStatus::rawPresentCapacity(uint8_t battery) const
{
    if (battery >= kBatteryCount) return 0;
    return extractBits16(presentCapacityBit(battery), 16);
}

// --- Typed decode (USB PD R3.3 Table 6.24) ------------------------------------------

const char *TPS25751TransmitBatteryStatus::toString(ChargingStatus val)
{
    switch (val) {
        case ChargingStatus::Charging: return "Charging";
        case ChargingStatus::Discharging: return "Discharging";
        case ChargingStatus::Idle: return "Idle";
        case ChargingStatus::Invalid: return "Invalid";
        default: return "Undefined";
    }
}

bool TPS25751TransmitBatteryStatus::invalidBatteryReference(uint8_t battery) const
{
    return (batteryInfo(battery) & 0x01u) != 0;
}

bool TPS25751TransmitBatteryStatus::batteryPresent(uint8_t battery) const
{
    return (batteryInfo(battery) & 0x02u) != 0;
}

TPS25751TransmitBatteryStatus::ChargingStatus TPS25751TransmitBatteryStatus::chargingStatus(uint8_t battery) const
{
    return static_cast<ChargingStatus>((batteryInfo(battery) >> 2) & 0x03u);
}

bool TPS25751TransmitBatteryStatus::presentCapacityKnown(uint8_t battery) const
{
    return battery < kBatteryCount && rawPresentCapacity(battery) != 0xFFFFu;
}

uint32_t TPS25751TransmitBatteryStatus::presentCapacity_mWh(uint8_t battery) const
{
    if (!presentCapacityKnown(battery)) return 0;
    return static_cast<uint32_t>(rawPresentCapacity(battery)) * 100u;  // 0.1 Wh = 100 mWh per step
}

// --- Setters: each a single setBits() call after the index guard -----------------

void TPS25751TransmitBatteryStatus::setBatteryInfo(uint8_t battery, uint8_t value)
{
    if (!isValid() || battery >= kBatteryCount) return;
    setBits(batteryInfoBit(battery), 8, value);
}

void TPS25751TransmitBatteryStatus::setRawPresentCapacity(uint8_t battery, uint16_t value)
{
    if (!isValid() || battery >= kBatteryCount) return;
    setBits(presentCapacityBit(battery), 16, value);
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
        printHex(s, raw()[i], 2);
    }
    s.println();

    for (uint8_t i = 0; i < kBatteryCount; i++)
    {
        const uint16_t capacity = rawPresentCapacity(i);
        s.print(F("Battery "));
        s.print(i);
        s.print(F(": Battery Info 0x"));
        printHex(s, batteryInfo(i), 2);
        s.print(F(", Present Capacity 0x"));
        printHex(s, capacity, 4);
        s.print(F(" ("));
        s.print(capacity);
        s.println(F(")"));
    }

    // Typed decode, USB PD R3.3 Table 6.24.
    for (uint8_t i = 0; i < kBatteryCount; i++)
    {
        s.print(F("Battery "));
        s.print(i);
        s.print(F(": "));
        s.print(batteryPresent(i) ? F("present") : F("not present"));
        if (invalidBatteryReference(i)) s.print(F(", invalid battery reference"));
        // Table 6.24 marks Charging Status reserved when the battery is absent or
        // the capacity is FFFFh, so print the status and capacity only when they
        // mean something.
        if (batteryPresent(i))
        {
            if (presentCapacityKnown(i))
            {
                s.print(F(", "));
                s.print(toString(chargingStatus(i)));
                s.print(F(", capacity "));
                s.print(static_cast<unsigned long>(presentCapacity_mWh(i)));
                s.print(F(" mWh"));
            }
            else
            {
                s.print(F(", capacity unknown"));
            }
        }
        s.println();
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
