#include "TPS25751TransmitSinkCaps.h"

uint8_t TPS25751TransmitSinkCaps::pdoCount() const {
    return extractBits(0, 3);  // Bits 2:0 of byte 0
}

uint32_t TPS25751TransmitSinkCaps::getPDO(uint8_t index) const {
    if (index >= 7) return 0;  // Max 7 PDOs (0-6)

    // PDOs start at byte 1, each is 4 bytes (no Power Path bytes on this register)
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

TPS25751Pdo TPS25751TransmitSinkCaps::pdo(uint8_t index) const {
    if (index >= 7) return TPS25751Pdo(0, TPS25751Pdo::kUnknownPosition, PdoRole::Sink);
    return TPS25751Pdo(getPDO(index), index + 1, PdoRole::Sink);
}

bool TPS25751TransmitSinkCaps::hasSinkCaps() const {
    // Mirrors TPS25751ReceivedSinkCaps::hasSinkCaps(): treat an all-zero PDO 1
    // as "no caps configured" regardless of the count.
    return pdoCount() > 0 && getPDO(0) != 0;
}

// --- Encode: pure static PDO word builders ---------------------------------

uint32_t TPS25751TransmitSinkCaps::buildFixedSupplyPDO(uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                                        bool dualRolePower, bool higherCapability,
                                                        bool unconstrainedPower, bool usbCommCapable,
                                                        bool dualRoleData, uint8_t fastRoleSwapCurrent) {
    // Sink PDO 1 flags (USB PD R3.3 Table 6.9), ORed into bits 29:23.
    uint32_t pdo = TPS25751Pdo::encodeFixed(voltage_mV, maxCurrent_mA);
    pdo |= (dualRolePower ? 1UL : 0UL) << 29;
    pdo |= (higherCapability ? 1UL : 0UL) << 28;
    pdo |= (unconstrainedPower ? 1UL : 0UL) << 27;
    pdo |= (usbCommCapable ? 1UL : 0UL) << 26;
    pdo |= (dualRoleData ? 1UL : 0UL) << 25;
    pdo |= (static_cast<uint32_t>(fastRoleSwapCurrent) & 0x03) << 23;
    return pdo;
}

uint32_t TPS25751TransmitSinkCaps::buildBatteryPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW) {
    return TPS25751Pdo::encodeBattery(maxVoltage_mV, minVoltage_mV, maxPower_mW);
}

uint32_t TPS25751TransmitSinkCaps::buildVariablePDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    return TPS25751Pdo::encodeVariable(maxVoltage_mV, minVoltage_mV, maxCurrent_mA);
}

uint32_t TPS25751TransmitSinkCaps::buildPPSPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    // The Sink PPS APDO has no Power Limited bit (Table 6.14).
    return TPS25751Pdo::encodeSprPps(maxVoltage_mV, minVoltage_mV, maxCurrent_mA, false);
}

// --- Encode: instance mutators ----------------------------------------------

void TPS25751TransmitSinkCaps::setPDOCount(uint8_t count) {
    setBits(0, 3, count);  // Bits 2:0 of byte 0; reserved bits 7:3 preserved
}

void TPS25751TransmitSinkCaps::setPDO(uint8_t index, uint32_t pdo) {
    if (!isValid() || index >= 7) return;

    size_t offset = 1 + (index * 4);
    if (offset + 3 >= _len) return;

    _raw[offset + 0] = static_cast<uint8_t>(pdo & 0xFF);
    _raw[offset + 1] = static_cast<uint8_t>((pdo >> 8) & 0xFF);
    _raw[offset + 2] = static_cast<uint8_t>((pdo >> 16) & 0xFF);
    _raw[offset + 3] = static_cast<uint8_t>((pdo >> 24) & 0xFF);
}

void TPS25751TransmitSinkCaps::setFixedSupplyPDO(uint8_t index, uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                                  bool dualRolePower, bool higherCapability,
                                                  bool unconstrainedPower, bool usbCommCapable,
                                                  bool dualRoleData, uint8_t fastRoleSwapCurrent) {
    setPDO(index, buildFixedSupplyPDO(voltage_mV, maxCurrent_mA, dualRolePower, higherCapability,
                                       unconstrainedPower, usbCommCapable, dualRoleData, fastRoleSwapCurrent));
}

void TPS25751TransmitSinkCaps::setBatteryPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW) {
    setPDO(index, buildBatteryPDO(maxVoltage_mV, minVoltage_mV, maxPower_mW));
}

void TPS25751TransmitSinkCaps::setVariablePDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    setPDO(index, buildVariablePDO(maxVoltage_mV, minVoltage_mV, maxCurrent_mA));
}

void TPS25751TransmitSinkCaps::setPPSPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    setPDO(index, buildPPSPDO(maxVoltage_mV, minVoltage_mV, maxCurrent_mA));
}

void TPS25751TransmitSinkCaps::debugPrint(Stream &s) const {
    s.println(F("=== Transmit Sink Capabilities Register (0x33) ==="));

    uint8_t count = pdoCount();
    s.print(F("  PDO Count: ")); s.println(count);

    if (!hasSinkCaps()) {
        s.println(F("  No sink capabilities configured"));
        s.println();
        return;
    }

    for (uint8_t i = 0; i < count && i < 7; i++) {
        s.println();
        s.print(F("--- PDO ")); s.print(i + 1); s.println(F(" ---"));

        pdo(i).debugPrint(s, "");
    }

    s.println();
}

bool TPS25751TransmitSinkCaps::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES.size;
    if (!isValidSize(expectedSize)) return false;

    // PDO count should be 0-7
    uint8_t count = pdoCount();
    if (count > 7) return false;

    // If no sink caps configured, that's valid
    if (!hasSinkCaps()) return true;

    // Validate the list against USB PD R3.3 (receiver and sender rules)
    TPS25751Pdo list[7];
    for (uint8_t i = 0; i < count; i++) list[i] = pdo(i);
    return validatePdoList(list, count, PdoListRules::TransmitSink);
}
