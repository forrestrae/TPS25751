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

TPS25751Pdo TPS25751ReceivedSourceCaps::pdo(uint8_t index) const {
    if (index >= 7) return TPS25751Pdo(0, TPS25751Pdo::kUnknownPosition, PdoRole::Source);
    return TPS25751Pdo(getPDO(index), index + 1, PdoRole::Source);
}

bool TPS25751ReceivedSourceCaps::hasSourceCaps() const {
    // The "Number Valid PDOs" count (byte 0, bits 2:0) can persist as a stale,
    // non-zero value after a source detaches: the controller clears the PDO
    // payload to all zeros but leaves the previous count in place (observed on
    // hardware: byte 0 == 0x06 with PDO bytes 1..24 all zero after disconnect).
    // A genuine Source Capabilities message always carries a valid, non-zero
    // 5 V Fixed PDO as PDO 1, so treat an all-zero PDO 1 as "no caps present"
    // regardless of the count. This keeps the disconnected state from being
    // misread as six empty PDOs (which would fail semantic validation).
    return pdoCount() > 0 && getPDO(0) != 0;
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

        pdo(i).debugPrint(s, "");
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

    // Validate the list against USB PD R3.3 (receiver rules, PDO 1 at 5 V)
    TPS25751Pdo list[7];
    for (uint8_t i = 0; i < count; i++) list[i] = pdo(i);
    return validatePdoList(list, count, PdoListRules::ReceivedSource);
}
