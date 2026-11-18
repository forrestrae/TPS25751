#include "TPS25751TransmitSourceCaps.h"

namespace {
constexpr uint8_t kPDOByteOffset = 3;  // PDO 1 starts at byte 3 (bytes 1-2 hold Power Path)
}

uint8_t TPS25751TransmitSourceCaps::pdoCount() const {
    return extractBits(0, 3);  // Bits 2:0 of byte 0
}

uint32_t TPS25751TransmitSourceCaps::getPDO(uint8_t index) const {
    if (index >= 7) return 0;  // Max 7 PDOs (0-6)

    // PDOs start at byte 3 (after the count byte + 2 power-path bytes), each is 4 bytes
    size_t offset = kPDOByteOffset + (index * 4);
    if (offset + 3 >= _len) return 0;  // Bounds check

    // Extract 32-bit PDO (little-endian)
    uint32_t pdo = 0;
    pdo |= static_cast<uint32_t>(_raw[offset + 0]);
    pdo |= static_cast<uint32_t>(_raw[offset + 1]) << 8;
    pdo |= static_cast<uint32_t>(_raw[offset + 2]) << 16;
    pdo |= static_cast<uint32_t>(_raw[offset + 3]) << 24;

    return pdo;
}

PDOType TPS25751TransmitSourceCaps::getPDOType(uint8_t index) const {
    uint32_t pdo = getPDO(index);
    return static_cast<PDOType>((pdo >> 30) & 0x03);  // Bits 31:30
}

bool TPS25751TransmitSourceCaps::hasSourceCaps() const {
    // Mirrors TPS25751ReceivedSourceCaps::hasSourceCaps(): treat an all-zero
    // PDO 1 as "no caps configured" regardless of the count, since a genuine
    // Source Capabilities message always carries a non-zero 5 V Fixed PDO 1.
    return pdoCount() > 0 && getPDO(0) != 0;
}

uint8_t TPS25751TransmitSourceCaps::getPowerPathForPDO(uint8_t index) const {
    // Only PDO 1 has a Power Path field (bits 9:8); bits 23:10 are reserved.
    // Source: SLVUCR8B (Rev. B, 2026-08), page 36.
    if (index != 0) return 0;
    return extractBits(8, 2);
}

// Helper method to extract bits from a specific PDO
uint32_t TPS25751TransmitSourceCaps::extractPDOBits(uint8_t index, uint8_t bitOffset, uint8_t bitCount) const {
    uint32_t pdo = getPDO(index);
    uint32_t mask = (1UL << bitCount) - 1;
    return (pdo >> bitOffset) & mask;
}

// Fixed Supply PDO parsing (type 00, Source variant)
uint16_t TPS25751TransmitSourceCaps::getFixedVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint16_t TPS25751TransmitSourceCaps::getFixedMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 10;  // Bits 9:0, 10mA units
}

bool TPS25751TransmitSourceCaps::getFixedDualRolePower(uint8_t index) const {
    return extractPDOBits(index, 29, 1);  // Bit 29
}

bool TPS25751TransmitSourceCaps::getFixedUSBSuspendSupported(uint8_t index) const {
    return extractPDOBits(index, 28, 1);  // Bit 28
}

bool TPS25751TransmitSourceCaps::getFixedUnconstrainedPower(uint8_t index) const {
    return extractPDOBits(index, 27, 1);  // Bit 27
}

bool TPS25751TransmitSourceCaps::getFixedUSBCommCapable(uint8_t index) const {
    return extractPDOBits(index, 26, 1);  // Bit 26
}

bool TPS25751TransmitSourceCaps::getFixedDualRoleData(uint8_t index) const {
    return extractPDOBits(index, 25, 1);  // Bit 25
}

bool TPS25751TransmitSourceCaps::getFixedUnchunkedSupported(uint8_t index) const {
    return extractPDOBits(index, 24, 1);  // Bit 24
}

// Battery PDO parsing (type 01)
uint16_t TPS25751TransmitSourceCaps::getBatteryMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 20, 10) * 50;  // Bits 29:20, 50mV units
}

uint16_t TPS25751TransmitSourceCaps::getBatteryMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint32_t TPS25751TransmitSourceCaps::getBatteryMaxPower_mW(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 250;  // Bits 9:0, 250mW units
}

// Variable Supply PDO parsing (type 10)
uint16_t TPS25751TransmitSourceCaps::getVariableMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 20, 10) * 50;  // Bits 29:20, 50mV units
}

uint16_t TPS25751TransmitSourceCaps::getVariableMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 10, 10) * 50;  // Bits 19:10, 50mV units
}

uint16_t TPS25751TransmitSourceCaps::getVariableMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 10) * 10;  // Bits 9:0, 10mA units
}

// Augmented/PPS PDO parsing (type 11, Source variant)
bool TPS25751TransmitSourceCaps::getPPSPowerLimited(uint8_t index) const {
    return extractPDOBits(index, 27, 1);  // Bit 27
}

uint16_t TPS25751TransmitSourceCaps::getPPSMaxVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 17, 8) * 100;  // Bits 24:17, 100mV units
}

uint16_t TPS25751TransmitSourceCaps::getPPSMinVoltage_mV(uint8_t index) const {
    return extractPDOBits(index, 8, 8) * 100;  // Bits 15:8, 100mV units
}

uint16_t TPS25751TransmitSourceCaps::getPPSMaxCurrent_mA(uint8_t index) const {
    return extractPDOBits(index, 0, 7) * 50;  // Bits 6:0, 50mA units
}

// --- Encode: pure static PDO word builders ---------------------------------

uint32_t TPS25751TransmitSourceCaps::buildFixedSupplyPDO(uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                                          bool dualRolePower, bool usbSuspend,
                                                          bool unconstrainedPower, bool usbCommCapable,
                                                          bool dualRoleData, bool unchunked) {
    // Type bits 31:30 = 00 (Fixed Supply) -- left at 0, not set explicitly.
    uint32_t pdo = 0;
    uint32_t voltageField = (static_cast<uint32_t>(voltage_mV) / 50) & 0x3FF;   // Bits 19:10
    uint32_t currentField = (static_cast<uint32_t>(maxCurrent_mA) / 10) & 0x3FF;  // Bits 9:0

    pdo |= (dualRolePower ? 1UL : 0UL) << 29;
    pdo |= (usbSuspend ? 1UL : 0UL) << 28;
    pdo |= (unconstrainedPower ? 1UL : 0UL) << 27;
    pdo |= (usbCommCapable ? 1UL : 0UL) << 26;
    pdo |= (dualRoleData ? 1UL : 0UL) << 25;
    pdo |= (unchunked ? 1UL : 0UL) << 24;
    pdo |= voltageField << 10;
    pdo |= currentField;

    return pdo;
}

uint32_t TPS25751TransmitSourceCaps::buildBatteryPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW) {
    uint32_t pdo = (static_cast<uint32_t>(PDOType::Battery) & 0x03) << 30;  // Bits 31:30 = 01
    uint32_t maxVField = (static_cast<uint32_t>(maxVoltage_mV) / 50) & 0x3FF;  // Bits 29:20
    uint32_t minVField = (static_cast<uint32_t>(minVoltage_mV) / 50) & 0x3FF;  // Bits 19:10
    uint32_t powerField = (maxPower_mW / 250) & 0x3FF;                        // Bits 9:0

    pdo |= maxVField << 20;
    pdo |= minVField << 10;
    pdo |= powerField;

    return pdo;
}

uint32_t TPS25751TransmitSourceCaps::buildVariablePDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    uint32_t pdo = (static_cast<uint32_t>(PDOType::VariableSupply) & 0x03) << 30;  // Bits 31:30 = 10
    uint32_t maxVField = (static_cast<uint32_t>(maxVoltage_mV) / 50) & 0x3FF;      // Bits 29:20
    uint32_t minVField = (static_cast<uint32_t>(minVoltage_mV) / 50) & 0x3FF;      // Bits 19:10
    uint32_t currentField = (static_cast<uint32_t>(maxCurrent_mA) / 10) & 0x3FF;   // Bits 9:0

    pdo |= maxVField << 20;
    pdo |= minVField << 10;
    pdo |= currentField;

    return pdo;
}

uint32_t TPS25751TransmitSourceCaps::buildPPSPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA,
                                                  bool powerLimited) {
    // Bits 31:30 = 11 (Augmented), bits 29:28 = 00 (SPR PPS APDO subtype).
    uint32_t pdo = (static_cast<uint32_t>(PDOType::Augmented) & 0x03) << 30;
    uint32_t maxVField = (static_cast<uint32_t>(maxVoltage_mV) / 100) & 0xFF;  // Bits 24:17
    uint32_t minVField = (static_cast<uint32_t>(minVoltage_mV) / 100) & 0xFF;  // Bits 15:8
    uint32_t currentField = (static_cast<uint32_t>(maxCurrent_mA) / 50) & 0x7F;  // Bits 6:0

    pdo |= (powerLimited ? 1UL : 0UL) << 27;
    pdo |= maxVField << 17;
    pdo |= minVField << 8;
    pdo |= currentField;

    return pdo;
}

// --- Encode: instance mutators ----------------------------------------------

void TPS25751TransmitSourceCaps::setPDOCount(uint8_t count) {
    setBits(0, 3, count);  // Bits 2:0 of byte 0; reserved bits 7:3 preserved
}

void TPS25751TransmitSourceCaps::setPDO(uint8_t index, uint32_t pdo) {
    if (!isValid() || index >= 7) return;

    size_t offset = kPDOByteOffset + (index * 4);
    if (offset + 3 >= _len) return;

    _raw[offset + 0] = static_cast<uint8_t>(pdo & 0xFF);
    _raw[offset + 1] = static_cast<uint8_t>((pdo >> 8) & 0xFF);
    _raw[offset + 2] = static_cast<uint8_t>((pdo >> 16) & 0xFF);
    _raw[offset + 3] = static_cast<uint8_t>((pdo >> 24) & 0xFF);
}

void TPS25751TransmitSourceCaps::setFixedSupplyPDO(uint8_t index, uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                                    bool dualRolePower, bool usbSuspend,
                                                    bool unconstrainedPower, bool usbCommCapable,
                                                    bool dualRoleData, bool unchunked) {
    setPDO(index, buildFixedSupplyPDO(voltage_mV, maxCurrent_mA, dualRolePower, usbSuspend,
                                       unconstrainedPower, usbCommCapable, dualRoleData, unchunked));
}

void TPS25751TransmitSourceCaps::setBatteryPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW) {
    setPDO(index, buildBatteryPDO(maxVoltage_mV, minVoltage_mV, maxPower_mW));
}

void TPS25751TransmitSourceCaps::setVariablePDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    setPDO(index, buildVariablePDO(maxVoltage_mV, minVoltage_mV, maxCurrent_mA));
}

void TPS25751TransmitSourceCaps::setPPSPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA,
                                            bool powerLimited) {
    setPDO(index, buildPPSPDO(maxVoltage_mV, minVoltage_mV, maxCurrent_mA, powerLimited));
}

void TPS25751TransmitSourceCaps::setPowerPathForPDO(uint8_t index, uint8_t pp) {
    // Only PDO 1 has a Power Path field (bits 9:8); never write reserved bits
    // 23:10. Source: SLVUCR8B (Rev. B, 2026-08), page 36.
    if (index != 0) return;
    setBits(8, 2, pp);
}

void TPS25751TransmitSourceCaps::debugPrint(Stream &s) const {
    s.println(F("=== Transmit Source Capabilities Register (0x32) ==="));

    uint8_t count = pdoCount();
    s.print(F("  PDO Count: ")); s.println(count);

    if (!hasSourceCaps()) {
        s.println(F("  No source capabilities configured"));
        s.println();
        return;
    }

    for (uint8_t i = 0; i < count && i < 7; i++) {
        s.println();
        s.print(F("--- PDO ")); s.print(i + 1); s.println(F(" ---"));

        if (i == 0) {
            // Rev. B defines a Power Path only for PDO 1 (SLVUCR8B p.36).
            s.print(F("Power Path: 0x")); s.println(getPowerPathForPDO(0), HEX);
        }

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

bool TPS25751TransmitSourceCaps::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES.size;
    if (!isValidSize(expectedSize)) return false;

    // PDO count should be 0-7
    uint8_t count = pdoCount();
    if (count > 7) return false;

    // If no source caps configured, that's valid
    if (!hasSourceCaps()) return true;

    // Validate each PDO based on its type
    for (uint8_t i = 0; i < count; i++) {
        PDOType type = getPDOType(i);

        switch (type) {
            case PDOType::FixedSupply: {
                uint16_t voltage = getFixedVoltage_mV(i);
                if (voltage == 0) return false;

                uint16_t current = getFixedMaxCurrent_mA(i);
                if (current == 0) return false;

                // USB PD requires PDO 1 to be the vSafe5V Fixed Supply object.
                // Tolerance band rather than exact equality (see
                // TPS25751ReceivedSourceCaps::isSemanticallyValid()).
                if (i == 0 && (voltage < 4750 || voltage > 5250)) return false;
                break;
            }

            case PDOType::Battery: {
                uint16_t minV = getBatteryMinVoltage_mV(i);
                uint16_t maxV = getBatteryMaxVoltage_mV(i);
                if (minV > maxV || minV == 0 || maxV == 0) return false;

                uint32_t power = getBatteryMaxPower_mW(i);
                if (power == 0) return false;
                break;
            }

            case PDOType::VariableSupply: {
                uint16_t minV = getVariableMinVoltage_mV(i);
                uint16_t maxV = getVariableMaxVoltage_mV(i);
                if (minV > maxV || minV == 0 || maxV == 0) return false;

                uint16_t current = getVariableMaxCurrent_mA(i);
                if (current == 0) return false;
                break;
            }

            case PDOType::Augmented: {
                uint16_t minV = getPPSMinVoltage_mV(i);
                uint16_t maxV = getPPSMaxVoltage_mV(i);
                if (minV > maxV || minV == 0 || maxV == 0) return false;

                uint16_t current = getPPSMaxCurrent_mA(i);
                if (current == 0) return false;
                break;
            }
        }
    }

    return true;
}
