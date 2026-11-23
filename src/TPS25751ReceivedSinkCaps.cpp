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

TPS25751Pdo TPS25751ReceivedSinkCaps::pdo(uint8_t index) const {
    if (index >= 7) return TPS25751Pdo(0, TPS25751Pdo::kUnknownPosition, PdoRole::Sink);
    return TPS25751Pdo(getPDO(index), index + 1, PdoRole::Sink);
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

        pdo(i).debugPrint(s, "");
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

    // Validate the list against USB PD R3.3 (receiver rules, PDO 1 at 5 V)
    TPS25751Pdo list[7];
    for (uint8_t i = 0; i < count; i++) list[i] = pdo(i);
    return validatePdoList(list, count, PdoListRules::ReceivedSink);
}
