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

TPS25751Pdo TPS25751TransmitSourceCaps::pdo(uint8_t index) const {
    if (index >= 7) return TPS25751Pdo(0, TPS25751Pdo::kUnknownPosition, PdoRole::Source);
    return TPS25751Pdo(getPDO(index), index + 1, PdoRole::Source);
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

// --- Encode: pure static PDO word builders ---------------------------------

uint32_t TPS25751TransmitSourceCaps::buildFixedSupplyPDO(uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                                          bool dualRolePower, bool usbSuspend,
                                                          bool unconstrainedPower, bool usbCommCapable,
                                                          bool dualRoleData, bool unchunked) {
    // Source PDO 1 flags (USB PD R3.3 Table 6.8), ORed into bits 29:24.
    uint32_t pdo = TPS25751Pdo::encodeFixed(voltage_mV, maxCurrent_mA);
    pdo |= (dualRolePower ? 1UL : 0UL) << 29;
    pdo |= (usbSuspend ? 1UL : 0UL) << 28;
    pdo |= (unconstrainedPower ? 1UL : 0UL) << 27;
    pdo |= (usbCommCapable ? 1UL : 0UL) << 26;
    pdo |= (dualRoleData ? 1UL : 0UL) << 25;
    pdo |= (unchunked ? 1UL : 0UL) << 24;
    return pdo;
}

uint32_t TPS25751TransmitSourceCaps::buildBatteryPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW) {
    return TPS25751Pdo::encodeBattery(maxVoltage_mV, minVoltage_mV, maxPower_mW);
}

uint32_t TPS25751TransmitSourceCaps::buildVariablePDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA) {
    return TPS25751Pdo::encodeVariable(maxVoltage_mV, minVoltage_mV, maxCurrent_mA);
}

uint32_t TPS25751TransmitSourceCaps::buildPPSPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA,
                                                  bool powerLimited) {
    return TPS25751Pdo::encodeSprPps(maxVoltage_mV, minVoltage_mV, maxCurrent_mA, powerLimited);
}

uint32_t TPS25751TransmitSourceCaps::buildSprAvsPDO(uint16_t current15V_mA, uint16_t current20V_mA, PdoPeakCurrent peak) {
    return TPS25751Pdo::encodeSprAvsSource(current15V_mA, current20V_mA, peak);
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

void TPS25751TransmitSourceCaps::setSprAvsPDO(uint8_t index, uint16_t current15V_mA, uint16_t current20V_mA, PdoPeakCurrent peak) {
    setPDO(index, buildSprAvsPDO(current15V_mA, current20V_mA, peak));
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

        pdo(i).debugPrint(s, "");
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

    // Validate the list against USB PD R3.3 (receiver and sender rules)
    TPS25751Pdo list[7];
    for (uint8_t i = 0; i < count; i++) list[i] = pdo(i);
    return validatePdoList(list, count, PdoListRules::TransmitSource);
}
