#include <Stream.h>
#include <string>

#include "TPS25751PowerStatus.h"

TPS25751PowerStatus::ChargerAdvertiseStatus TPS25751PowerStatus::getChargerAdvertiseStatus() const
{
    return extractEnum<ChargerAdvertiseStatus>(8, 2, static_cast<uint8_t>(ChargerAdvertiseStatus::RESERVED), ChargerAdvertiseStatus::DISABLED_OR_NOT_RUN);
}

TPS25751PowerStatus::ChargerDetectStatus TPS25751PowerStatus::getChargerDetectStatus() const
{
    return extractEnum<ChargerDetectStatus>(4, 4, static_cast<uint8_t>(ChargerDetectStatus::COMPLETE_1_2V_DCP), ChargerDetectStatus::DISABLED_OR_NOT_RUN);
}

TPS25751PowerStatus::TypeCCurrent TPS25751PowerStatus::getTypeCCurrent() const
{
    return extractEnum<TypeCCurrent>(2, 2, static_cast<uint8_t>(TypeCCurrent::EXPLICIT_PD_CONTRACT), TypeCCurrent::USB_DEFAULT);
}

bool TPS25751PowerStatus::isSourceSink() const
{
    return extractBits(1, 1);
}

bool TPS25751PowerStatus::isPowerConnection() const
{
    return extractBits(0, 1);
}

const char* TPS25751PowerStatus::getChargerAdvertiseStatusString() const
{
    switch (getChargerAdvertiseStatus()) {
        case ChargerAdvertiseStatus::DISABLED_OR_NOT_RUN:
            return "Disabled/Not Run";
        case ChargerAdvertiseStatus::IN_PROCESS:
            return "In Process";
        case ChargerAdvertiseStatus::COMPLETE:
            return "Complete";
        case ChargerAdvertiseStatus::RESERVED:
            return "Reserved";
        default:
            return "Unknown";
    }
}

const char* TPS25751PowerStatus::getChargerDetectStatusString() const
{
    switch (getChargerDetectStatus()) {
        case ChargerDetectStatus::DISABLED_OR_NOT_RUN:
            return "Disabled/Not Run";
        case ChargerDetectStatus::IN_PROGRESS:
            return "In Progress";
        case ChargerDetectStatus::COMPLETE_NONE:
            return "Complete - None Detected";
        case ChargerDetectStatus::COMPLETE_SDP:
            return "Complete - SDP";
        case ChargerDetectStatus::COMPLETE_BC12_CDP:
            return "Complete - BC 1.2 CDP";
        case ChargerDetectStatus::COMPLETE_BC12_DCP:
            return "Complete - BC 1.2 DCP";
        case ChargerDetectStatus::COMPLETE_DIVIDER1_DCP:
            return "Complete - Divider1 DCP";
        case ChargerDetectStatus::COMPLETE_DIVIDER2_DCP:
            return "Complete - Divider2 DCP";
        case ChargerDetectStatus::COMPLETE_DIVIDER3_DCP:
            return "Complete - Divider3 DCP";
        case ChargerDetectStatus::COMPLETE_1_2V_DCP:
            return "Complete - 1.2V DCP";
        default:
            return "Unknown";
    }
}

const char* TPS25751PowerStatus::getTypeCCurrentString() const
{
    switch (getTypeCCurrent()) {
        case TypeCCurrent::USB_DEFAULT:
            return "USB Default Current";
        case TypeCCurrent::CURRENT_1_5A:
            return "1.5 A";
        case TypeCCurrent::CURRENT_3_0A:
            return "3.0 A";
        case TypeCCurrent::EXPLICIT_PD_CONTRACT:
            return "Explicit PD Contract";
        default:
            return "Unknown";
    }
}

void TPS25751PowerStatus::debugPrint(Stream &s) const
{
    s.println(F("=== Power Status Register (0x3F) ==="));

    // Connection status
    s.print(F("  Power Connection: "));
    s.println(isPowerConnection() ? F("Yes") : F("No"));

    if (isPowerConnection()) {
        // Source/Sink
        s.print(F("  Role: "));
        if (isProvidingPower()) {
            s.println(F("Providing Power (Sink)"));
        } else {
            s.println(F("Requesting Power (Source)"));
        }

        // Type-C Current
        s.print(F("  Type-C Current: "));
        s.println(getTypeCCurrentString());

        // Charger detection
        s.print(F("  Charger Detect: "));
        s.println(getChargerDetectStatusString());

        // Charger advertise
        s.print(F("  Charger Advertise: "));
        s.println(getChargerAdvertiseStatusString());
    }

    s.println();
}

bool TPS25751PowerStatus::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::POWER_STATUS.size;
    if (!isValidSize(expectedSize)) return false;

    // Logical consistency checks

    // If no power connection, charger detection should be disabled/not run or in progress
    if (!isPowerConnection()) {
        auto chargerStatus = getChargerDetectStatus();
        if (chargerStatus != ChargerDetectStatus::DISABLED_OR_NOT_RUN &&
            chargerStatus != ChargerDetectStatus::IN_PROGRESS) {
            // It's unusual to have completed charger detection without a connection
            // but not necessarily invalid
        }
    }

    // Check for reserved values in charger advertise status
    if (getChargerAdvertiseStatus() == ChargerAdvertiseStatus::RESERVED) {
        // Reserved value detected - may indicate invalid data
        return false;
    }

    // Charger detect status should be in valid range (0-9)
    uint8_t chargerDetectValue = extractBits(4, 4);
    if (chargerDetectValue > 9) {
        return false;
    }

    return true;
}