#include "TPS25751ActivePDOContract.h"

uint32_t TPS25751ActivePDOContract::activePDO() const {
    return extractBits(0, 32);
}

uint16_t TPS25751ActivePDOContract::firstPDOControlBits() const {
    return extractBits(32, 10);
}

PDOType TPS25751ActivePDOContract::pdoType() const {
    uint8_t type = extractBits(30, 2);
    return static_cast<PDOType>(type);
}

bool TPS25751ActivePDOContract::hasActiveContract() const {
    // If all bits are zero, no contract exists
    return activePDO() != 0;
}

// Fixed Supply PDO (Type 00)
uint16_t TPS25751ActivePDOContract::fixedVoltage_mV() const {
    uint16_t voltageBits = extractBits(10, 10);  // Bits 19:10
    return voltageBits * 50;  // 50mV units
}

uint16_t TPS25751ActivePDOContract::fixedMaxCurrent_mA() const {
    uint16_t currentBits = extractBits(0, 10);  // Bits 9:0
    return currentBits * 10;  // 10mA units
}

bool TPS25751ActivePDOContract::fixedDualRolePower() const {
    return extractBits(29, 1);
}

bool TPS25751ActivePDOContract::fixedUSBSuspendSupported() const {
    return extractBits(28, 1);
}

bool TPS25751ActivePDOContract::fixedUnconstrainedPower() const {
    return extractBits(27, 1);
}

bool TPS25751ActivePDOContract::fixedUSBCommCapable() const {
    return extractBits(26, 1);
}

bool TPS25751ActivePDOContract::fixedDualRoleData() const {
    return extractBits(25, 1);
}

bool TPS25751ActivePDOContract::fixedUnchunkedSupported() const {
    return extractBits(24, 1);
}

// Battery PDO (Type 01)
uint16_t TPS25751ActivePDOContract::batteryMaxVoltage_mV() const {
    uint16_t voltageBits = extractBits(20, 10);  // Bits 29:20
    return voltageBits * 50;  // 50mV units
}

uint16_t TPS25751ActivePDOContract::batteryMinVoltage_mV() const {
    uint16_t voltageBits = extractBits(10, 10);  // Bits 19:10
    return voltageBits * 50;  // 50mV units
}

uint32_t TPS25751ActivePDOContract::batteryMaxPower_mW() const {
    uint16_t powerBits = extractBits(0, 10);  // Bits 9:0
    return static_cast<uint32_t>(powerBits) * 250;  // 250mW units
}

// Variable Supply PDO (Type 10)
uint16_t TPS25751ActivePDOContract::variableMaxVoltage_mV() const {
    uint16_t voltageBits = extractBits(20, 10);  // Bits 29:20
    return voltageBits * 50;  // 50mV units
}

uint16_t TPS25751ActivePDOContract::variableMinVoltage_mV() const {
    uint16_t voltageBits = extractBits(10, 10);  // Bits 19:10
    return voltageBits * 50;  // 50mV units
}

uint16_t TPS25751ActivePDOContract::variableMaxCurrent_mA() const {
    uint16_t currentBits = extractBits(0, 10);  // Bits 9:0
    return currentBits * 10;  // 10mA units
}

// Augmented/PPS PDO (Type 11)
bool TPS25751ActivePDOContract::ppsPowerLimited() const {
    return extractBits(27, 1);
}

uint16_t TPS25751ActivePDOContract::ppsMaxVoltage_mV() const {
    uint16_t voltageBits = extractBits(17, 10);  // Bits 26:17
    return voltageBits * 100;  // 100mV units for PPS
}

uint16_t TPS25751ActivePDOContract::ppsMinVoltage_mV() const {
    uint16_t voltageBits = extractBits(8, 9);  // Bits 16:8
    return voltageBits * 100;  // 100mV units for PPS
}

uint16_t TPS25751ActivePDOContract::ppsMaxCurrent_mA() const {
    uint16_t currentBits = extractBits(0, 8);  // Bits 7:0
    return currentBits * 50;  // 50mA units for PPS
}

void TPS25751ActivePDOContract::debugPrint(Stream &s) const {
    s.println(F("=== Active PDO Contract Register (0x34) ==="));

    if (!hasActiveContract()) {
        s.println(F("  No active PD contract"));
        s.println();
        return;
    }

    PDOType type = pdoType();
    s.print(F("  PDO Type: ")); s.println(toString(type));
    s.print(F("First PDO Control Bits: 0x")); s.println(firstPDOControlBits(), HEX);

    switch (type) {
        case PDOType::FixedSupply:
            s.println(F("  Fixed Supply PDO:"));
            s.print(F("    Voltage: ")); s.print(fixedVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Max Current: ")); s.print(fixedMaxCurrent_mA()); s.println(F(" mA"));
            s.print(F("    Dual Role Power: ")); s.println(fixedDualRolePower() ? F("Yes") : F("No"));
            s.print(F("    USB Suspend Supported: ")); s.println(fixedUSBSuspendSupported() ? F("Yes") : F("No"));
            s.print(F("    Unconstrained Power: ")); s.println(fixedUnconstrainedPower() ? F("Yes") : F("No"));
            s.print(F("    USB Comm Capable: ")); s.println(fixedUSBCommCapable() ? F("Yes") : F("No"));
            s.print(F("    Dual Role Data: ")); s.println(fixedDualRoleData() ? F("Yes") : F("No"));
            s.print(F("    Unchunked Supported: ")); s.println(fixedUnchunkedSupported() ? F("Yes") : F("No"));
            break;

        case PDOType::Battery:
            s.println(F("  Battery PDO:"));
            s.print(F("    Max Voltage: ")); s.print(batteryMaxVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Min Voltage: ")); s.print(batteryMinVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Max Power: ")); s.print(batteryMaxPower_mW()); s.println(F(" mW"));
            break;

        case PDOType::VariableSupply:
            s.println(F("  Variable Supply PDO:"));
            s.print(F("    Max Voltage: ")); s.print(variableMaxVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Min Voltage: ")); s.print(variableMinVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Max Current: ")); s.print(variableMaxCurrent_mA()); s.println(F(" mA"));
            break;

        case PDOType::Augmented:
            s.println(F("  Augmented/PPS PDO:"));
            s.print(F("    Power Limited: ")); s.println(ppsPowerLimited() ? F("Yes") : F("No"));
            s.print(F("    Max Voltage: ")); s.print(ppsMaxVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Min Voltage: ")); s.print(ppsMinVoltage_mV()); s.println(F(" mV"));
            s.print(F("    Max Current: ")); s.print(ppsMaxCurrent_mA()); s.println(F(" mA"));
            break;
    }

    s.println();
}

const char* toString(PDOType type) {
    switch (type) {
        case PDOType::FixedSupply: return "Fixed Supply";
        case PDOType::Battery: return "Battery";
        case PDOType::VariableSupply: return "Variable Supply";
        case PDOType::Augmented: return "Augmented/PPS";
        default: return "Unknown";
    }
}

bool TPS25751ActivePDOContract::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT.size;
    if (!isValidSize(expectedSize)) return false;

    // If no active contract, that's valid
    if (!hasActiveContract()) return true;

    PDOType type = pdoType();

    // Validate based on PDO type
    switch (type) {
        case PDOType::FixedSupply: {
            uint16_t voltage = fixedVoltage_mV();
            uint16_t current = fixedMaxCurrent_mA();
            // Voltage and current should be non-zero for a real contract
            if (voltage == 0 || current == 0) return false;
            // Reasonable voltage range check (typically 5V-20V for USB PD)
            if (voltage < 3000 || voltage > 50000) return false;
            break;
        }

        case PDOType::Battery: {
            uint16_t maxV = batteryMaxVoltage_mV();
            uint16_t minV = batteryMinVoltage_mV();
            uint32_t power = batteryMaxPower_mW();
            // Max must be >= Min
            if (maxV < minV) return false;
            // Power should be non-zero
            if (power == 0) return false;
            break;
        }

        case PDOType::VariableSupply: {
            uint16_t maxV = variableMaxVoltage_mV();
            uint16_t minV = variableMinVoltage_mV();
            uint16_t current = variableMaxCurrent_mA();
            // Max must be >= Min
            if (maxV < minV) return false;
            // Current should be non-zero
            if (current == 0) return false;
            break;
        }

        case PDOType::Augmented: {
            uint16_t maxV = ppsMaxVoltage_mV();
            uint16_t minV = ppsMinVoltage_mV();
            uint16_t current = ppsMaxCurrent_mA();
            // Max must be >= Min
            if (maxV < minV) return false;
            // Current should be non-zero
            if (current == 0) return false;
            break;
        }
    }

    return true;
}
