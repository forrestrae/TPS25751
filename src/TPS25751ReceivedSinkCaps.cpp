#include "TPS25751ReceivedSinkCaps.h"

uint8_t TPS25751ReceivedSinkCaps::pdoCount() const {
    return extractBits(0, 3);  // Bits 2:0 of byte 0
}

uint32_t TPS25751ReceivedSinkCaps::getPDO(uint8_t index) const {
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

PDOType TPS25751ReceivedSinkCaps::getPDOType(uint8_t index) const {
    uint32_t pdo = getPDO(index);
    return static_cast<PDOType>((pdo >> 30) & 0x03);  // Bits 31:30
}

bool TPS25751ReceivedSinkCaps::hasSinkCaps() const {
    // The "Number Valid PDOs" count (byte 0, bits 2:0) can persist as a stale,
    // non-zero value after the partner detaches: the controller clears the PDO
    // payload to all zeros but leaves the previous count in place (same
    // behavior observed on the Received Source Capabilities register). A genuine
    // capabilities message always carries a valid, non-zero 5 V Fixed PDO as
    // PDO 1, so treat an all-zero PDO 1 as "no caps present" regardless of the
    // count, keeping the detached state from failing semantic validation.
    return pdoCount() > 0 && getPDO(0) != 0;
}

// Helper method to extract bits from a specific PDO
uint32_t TPS25751ReceivedSinkCaps::extractPDOBits(uint8_t index, uint8_t bitOffset, uint8_t bitCount) const {
    uint32_t pdo = getPDO(index);
    uint32_t mask = (1UL << bitCount) - 1;
    return (pdo >> bitOffset) & mask;
}

// Fixed Supply PDO parsing (type 00)
uint16_t TPS25751ReceivedSinkCaps::getFixedVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint16_t TPS25751ReceivedSinkCaps::getFixedMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 10;  // Bits 9:0, 10mA units
}

bool TPS25751ReceivedSinkCaps::getFixedDualRolePower(uint8_t index) const {
    return extractPDOBits(index, 29, 1);  // Bit 29
}

bool TPS25751ReceivedSinkCaps::getFixedHigherCapability(uint8_t index) const {
    return extractPDOBits(index, 28, 1);  // Bit 28 (different from source!)
}

bool TPS25751ReceivedSinkCaps::getFixedUnconstrainedPower(uint8_t index) const {
    return extractPDOBits(index, 27, 1);  // Bit 27
}

bool TPS25751ReceivedSinkCaps::getFixedUSBCommCapable(uint8_t index) const {
    return extractPDOBits(index, 26, 1);  // Bit 26
}

bool TPS25751ReceivedSinkCaps::getFixedDualRoleData(uint8_t index) const {
    return extractPDOBits(index, 25, 1);  // Bit 25
}

bool TPS25751ReceivedSinkCaps::getFixedFastRoleSwapCurrent(uint8_t index) const {
    return extractPDOBits(index, 23, 2);  // Bits 24:23 (different from source!)
}

// Battery PDO parsing (type 01)
uint16_t TPS25751ReceivedSinkCaps::getBatteryMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 20, 10) * 50;  // Bits 29:20, 50mV units
}

uint16_t TPS25751ReceivedSinkCaps::getBatteryMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint32_t TPS25751ReceivedSinkCaps::getBatteryMaxPower_mW(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 250;  // Bits 9:0, 250mW units
}

// Variable Supply PDO parsing (type 10)
uint16_t TPS25751ReceivedSinkCaps::getVariableMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 20, 10) * 50;  // Bits 29:20, 50mV units
}

uint16_t TPS25751ReceivedSinkCaps::getVariableMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint16_t TPS25751ReceivedSinkCaps::getVariableMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 10;  // Bits 9:0, 10mA units
}

// Augmented/PPS PDO parsing (type 11)
uint16_t TPS25751ReceivedSinkCaps::getPPSMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 17, 8) * 100;  // Bits 24:17, 100mV units
}

uint16_t TPS25751ReceivedSinkCaps::getPPSMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 8, 8) * 100;  // Bits 15:8, 100mV units
}

uint16_t TPS25751ReceivedSinkCaps::getPPSMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 7) * 50;  // Bits 6:0, 50mA units
}

void TPS25751ReceivedSinkCaps::debugPrint(Stream &s) const {
    s.println(F("=== Received Sink Capabilities Register (0x31) ==="));

    uint8_t count = pdoCount();
    s.print(F("  PDO Count: ")); s.println(count);

    if (!hasSinkCaps()) {
        s.println(F("  No sink capabilities received"));
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
                s.print(F("  Higher Capability: ")); s.println(getFixedHigherCapability(i) ? F("Yes") : F("No"));
                s.print(F("  Unconstrained Power: ")); s.println(getFixedUnconstrainedPower(i) ? F("Yes") : F("No"));
                s.print(F("  USB Comm Capable: ")); s.println(getFixedUSBCommCapable(i) ? F("Yes") : F("No"));
                s.print(F("  Dual Role Data: ")); s.println(getFixedDualRoleData(i) ? F("Yes") : F("No"));
                s.print(F("  Fast Role Swap Current: ")); s.println(getFixedFastRoleSwapCurrent(i));
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
                s.print(F("  Max Voltage: ")); s.print(getPPSMaxVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Min Voltage: ")); s.print(getPPSMinVoltage_mV(i)); s.println(F(" mV"));
                s.print(F("  Max Current: ")); s.print(getPPSMaxCurrent_mA(i)); s.println(F(" mA"));
                break;
        }
    }

    s.println();
}

bool TPS25751ReceivedSinkCaps::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES.size;
    if (!isValidSize(expectedSize)) return false;

    // PDO count should be 0-7
    uint8_t count = pdoCount();
    if (count > 7) return false;

    // If no sink caps, that's valid
    if (!hasSinkCaps()) return true;

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

                // USB PD requires PDO 1 to be the vSafe5V Fixed Supply object.
                // Enforce that as a sanity check, but with a tolerance band
                // rather than exact equality: a nominal 5 V advertisement may
                // land a step or two off 5000 mV, and rejecting the entire
                // (otherwise decodable) register over that is too strict.
                if (i == 0 && (voltage < 4750 || voltage > 5250)) return false;
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
