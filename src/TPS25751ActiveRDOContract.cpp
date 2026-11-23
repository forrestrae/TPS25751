#include "TPS25751ActiveRDOContract.h"

uint32_t TPS25751ActiveRDOContract::rawOwnRdo() const {
    return extractBits32(0, 32);  // Bits 31:0 (SLVUCR8B p.39)
}

uint32_t TPS25751ActiveRDOContract::rawRequestedRdo() const {
    // Bits 127:96 (bytes 12-15, little-endian). Source: SLVUCR8B (Rev. B, 2026-08), page 39.
    return extractBits32(96, 32);
}

TPS25751Rdo TPS25751ActiveRDOContract::ownRdo(RdoKind kind) const {
    return TPS25751Rdo(rawOwnRdo(), kind);
}

TPS25751Rdo TPS25751ActiveRDOContract::requestedRdo(RdoKind kind) const {
    return TPS25751Rdo(rawRequestedRdo(), kind);
}

bool TPS25751ActiveRDOContract::hasActiveContract() const {
    // If all bits are zero, no contract exists
    return rawOwnRdo() != 0;
}

void TPS25751ActiveRDOContract::debugPrint(Stream &s) const {
    debugPrint(s, RdoKind::Unknown, RdoKind::Unknown);
}

void TPS25751ActiveRDOContract::debugPrint(Stream &s, RdoKind ownKind, RdoKind requestedKind) const {
    s.println(F("=== Active RDO Contract Register (0x35) ==="));

    const uint32_t own = rawOwnRdo();
    const uint32_t requested = rawRequestedRdo();

    if (own == 0 && requested == 0) {
        s.println(F("  No active RDO contract"));
        s.println();
        return;
    }

    s.print(F("  Own RDO (bits 31:0, sent as Sink): 0x")); printHex(s, own, 8); s.println();
    if (own != 0) ownRdo(ownKind).debugPrint(s, "    ");

    s.print(F("  Requested RDO (bits 127:96, a partner's request only while the port is Source): 0x"));
    printHex(s, requested, 8); s.println();
    if (requested != 0) requestedRdo(requestedKind).debugPrint(s, "    ");

    s.println();
}

bool TPS25751ActiveRDOContract::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT.size;
    if (!isValidSize(expectedSize)) return false;

    // Object Position is 1 to 7 in any nonzero RDO (USB PD R3.3 Tables 6.19 to 6.22)
    if (rawOwnRdo() != 0) {
        uint8_t pos = ownRdo().objectPosition();
        if (pos < 1 || pos > 7) return false;
    }
    if (rawRequestedRdo() != 0) {
        uint8_t pos = requestedRdo().objectPosition();
        if (pos < 1 || pos > 7) return false;
    }

    return true;
}
