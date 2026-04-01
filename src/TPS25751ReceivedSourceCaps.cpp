#include "TPS25751ReceivedSourceCaps.h"

uint8_t TPS25751ReceivedSourceCaps::pdoCount() const {
    return extractBits(0, 3);  // Bits 2:0 of byte 0
}

uint32_t TPS25751ReceivedSourceCaps::getPDO(uint8_t index) const {
    if (index >= 7) return 0;  // Max 7 PDOs (0-6)

    // PDOs start at byte 1, each is 4 bytes
    size_t offset = 1 + (index * 4);
    if (offset + 3 >= _len) return 0;  // Bounds check

    // Extract 32-bit PDO (little-endian)
    uint32_t pdo = 0;
    pdo |= static_cast<uint32_t>(_raw[offset + 0]);
    pdo |= static_cast<uint32_t>(_raw[offset + 1]) << 8;
    pdo |= static_cast<uint32_t>(_raw[offset + 2]) << 16;
    pdo |= static_cast<uint32_t>(_raw[offset + 3]) << 24;

    return pdo;
}

PDOType TPS25751ReceivedSourceCaps::getPDOType(uint8_t index) const {
    uint32_t pdo = getPDO(index);
    return static_cast<PDOType>((pdo >> 30) & 0x03);  // Bits 31:30
}

bool TPS25751ReceivedSourceCaps::hasSourceCaps() const {
    return pdoCount() > 0;
}

// Helper method to extract bits from a specific PDO
uint32_t TPS25751ReceivedSourceCaps::extractPDOBits(uint8_t index, uint8_t bitOffset, uint8_t bitCount) const {
    uint32_t pdo = getPDO(index);
    uint32_t mask = (1UL << bitCount) - 1;
    return (pdo >> bitOffset) & mask;
}

// Fixed Supply PDO parsing (type 00)
uint16_t TPS25751ReceivedSourceCaps::getFixedVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint16_t TPS25751ReceivedSourceCaps::getFixedMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 10;  // Bits 9:0, 10mA units
}

bool TPS25751ReceivedSourceCaps::getFixedDualRolePower(uint8_t index) const {
    return extractPDOBits(index, 29, 1);  // Bit 29
}

bool TPS25751ReceivedSourceCaps::getFixedUSBSuspendSupported(uint8_t index) const {
    return extractPDOBits(index, 28, 1);  // Bit 28
}

bool TPS25751ReceivedSourceCaps::getFixedUnconstrainedPower(uint8_t index) const {
    return extractPDOBits(index, 27, 1);  // Bit 27
}

bool TPS25751ReceivedSourceCaps::getFixedUSBCommCapable(uint8_t index) const {
    return extractPDOBits(index, 26, 1);  // Bit 26
}

bool TPS25751ReceivedSourceCaps::getFixedDualRoleData(uint8_t index) const {
    return extractPDOBits(index, 25, 1);  // Bit 25
}

bool TPS25751ReceivedSourceCaps::getFixedUnchunkedSupported(uint8_t index) const {
    return extractPDOBits(index, 24, 1);  // Bit 24
}

// Battery PDO parsing (type 01)
uint16_t TPS25751ReceivedSourceCaps::getBatteryMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 20, 10) * 50;  // Bits 29:20, 50mV units
}

uint16_t TPS25751ReceivedSourceCaps::getBatteryMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint32_t TPS25751ReceivedSourceCaps::getBatteryMaxPower_mW(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 250;  // Bits 9:0, 250mW units
}

// Variable Supply PDO parsing (type 10)
uint16_t TPS25751ReceivedSourceCaps::getVariableMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 20, 10) * 50;  // Bits 29:20, 50mV units
}

uint16_t TPS25751ReceivedSourceCaps::getVariableMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint16_t TPS25751ReceivedSourceCaps::getVariableMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 10;  // Bits 9:0, 10mA units
}

// Augmented/PPS PDO parsing (type 11)
bool TPS25751ReceivedSourceCaps::getPPSPowerLimited(uint8_t index) const {
    return extractPDOBits(index, 27, 1);  // Bit 27
}

uint16_t TPS25751ReceivedSourceCaps::getPPSMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 17, 8) * 100;  // Bits 24:17, 100mV units
}

uint16_t TPS25751ReceivedSourceCaps::getPPSMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 8, 8) * 100;  // Bits 15:8, 100mV units
}

uint16_t TPS25751ReceivedSourceCaps::getPPSMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 7) * 50;  // Bits 6:0, 50mA units
}

void TPS25751ReceivedSourceCaps::debugPrint(Stream &s) const {
    s.println(F("=== Received Source Capabilities Register (0x30) ==="));

    uint8_t count = pdoCount();
    s.print(F("  PDO Count: ")); s.println(count);

    if (!hasSourceCaps()) {
        s.println(F("  No source capabilities received"));
        s.println();
        return;
    }

    for (uint8_t i = 0; i < count && i < 7; i++) {
        s.println();
        s.print(F("--- PDO ")); s.print(i + 1); s.println(F(" ---"));

        PDOType type = getPDOType(i);
        s.print(F("Type: "));
        switch (type) {
            case PDOType::FixedSupply:
                s.println(F("Fixed Supply"));
                s.print(F("  Voltage: ")); s.print(getFixedVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Max Current: ")); s.print(getFixedMaxCurrent_mA(i)); s.println(F(" mA"));
                s.print(F("  Dual Role Power: ")); s.println(getFixedDualRolePower(i) ? F("Yes") : F("No"));
                s.print(F("  USB Suspend Supported: ")); s.println(getFixedUSBSuspendSupported(i) ? F("Yes") : F("No"));
                s.print(F("  Unconstrained Power: ")); s.println(getFixedUnconstrainedPower(i) ? F("Yes") : F("No"));
                s.print(F("  USB Comm Capable: ")); s.println(getFixedUSBCommCapable(i) ? F("Yes") : F("No"));
                s.print(F("  Dual Role Data: ")); s.println(getFixedDualRoleData(i) ? F("Yes") : F("No"));
                s.print(F("  Unchunked Supported: ")); s.println(getFixedUnchunkedSupported(i) ? F("Yes") : F("No"));
                break;

            case PDOType::Battery:
                s.println(F("Battery"));
                s.print(F("  Max Voltage: ")); s.print(getBatteryMaxVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Min Voltage: ")); s.print(getBatteryMinVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Max Power: ")); s.print(getBatteryMaxPower_mW(i)); s.println(F(" mW"));
                break;

            case PDOType::VariableSupply:
                s.println(F("Variable Supply"));
                s.print(F("  Max Voltage: ")); s.print(getVariableMaxVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Min Voltage: ")); s.print(getVariableMinVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Max Current: ")); s.print(getVariableMaxCurrent_mA(i)); s.println(F(" mA"));
                break;

            case PDOType::Augmented:
                s.println(F("Augmented (PPS)"));
                s.print(F("  Power Limited: ")); s.println(getPPSPowerLimited(i) ? F("Yes") : F("No"));
                s.print(F("  Max Voltage: ")); s.print(getPPSMaxVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Min Voltage: ")); s.print(getPPSMinVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Max Current: ")); s.print(getPPSMaxCurrent_mA(i)); s.println(F(" mA"));
                break;
        }
    }

    s.println();
}

bool TPS25751ReceivedSourceCaps::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES.size;
    if (!isValidSize(expectedSize)) return false;

    // PDO count should be 0-7
    uint8_t count = pdoCount();
    if (count > 7) return false;

    // If no source caps, that's valid
    if (!hasSourceCaps()) return true;

    // Validate each PDO based on its type
    for (uint8_t i = 0; i < count; i++) {
        PDOType type = getPDOType(i);

        switch (type) {
            case PDOType::FixedSupply: {
                // Fixed voltage should be non-zero
                uint16_t voltage = getFixedVoltage_mV(i);
                if (voltage == 0) return false;

                // Max current should be non-zero
                uint16_t current = getFixedMaxCurrent_mA(i);
                if (current == 0) return false;

                // First PDO must be 5V for USB PD spec compliance
                if (i == 0 && voltage != 5000) return false;
                break;
            }

            case PDOType::Battery: {
                // Min voltage should be less than or equal to max voltage
                uint16_t minV = getBatteryMinVoltage_mV(i);
                uint16_t maxV = getBatteryMaxVoltage_mV(i);
                if (minV > maxV || minV == 0 || maxV == 0) return false;

                // Max power should be non-zero
                uint32_t power = getBatteryMaxPower_mW(i);
                if (power == 0) return false;
                break;
            }

            case PDOType::VariableSupply: {
                // Min voltage should be less than or equal to max voltage
                uint16_t minV = getVariableMinVoltage_mV(i);
                uint16_t maxV = getVariableMaxVoltage_mV(i);
                if (minV > maxV || minV == 0 || maxV == 0) return false;

                // Max current should be non-zero
                uint16_t current = getVariableMaxCurrent_mA(i);
                if (current == 0) return false;
                break;
            }

            case PDOType::Augmented: {
                // Min voltage should be less than or equal to max voltage
                uint16_t minV = getPPSMinVoltage_mV(i);
                uint16_t maxV = getPPSMaxVoltage_mV(i);
                if (minV > maxV || minV == 0 || maxV == 0) return false;

                // Max current should be non-zero
                uint16_t current = getPPSMaxCurrent_mA(i);
                if (current == 0) return false;
                break;
            }
        }
    }

    return true;
}
