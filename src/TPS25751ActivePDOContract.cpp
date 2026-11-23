#include "TPS25751ActivePDOContract.h"

// Register layout: bits 31:0 Active PDO, bits 41:32 First PDO Control Bits,
// bits 47:42 reserved. Source: SLVUCR8B (Rev. B, 2026-08), page 38. The fields
// inside the PDO word decode through TPS25751Pdo (USB PD R3.3).

uint32_t TPS25751ActivePDOContract::rawActivePdo() const {
    return extractBits32(0, 32);
}

uint16_t TPS25751ActivePDOContract::rawFirstPdoControlBits() const {
    return extractBits16(32, 10);  // Bits 41:32
}

TPS25751Pdo TPS25751ActivePDOContract::activePdo() const {
    return TPS25751Pdo(rawActivePdo(), TPS25751Pdo::kUnknownPosition, PdoRole::Source);
}

TPS25751Pdo TPS25751ActivePDOContract::firstPdoControl() const {
    // First PDO Control Bits are PDO 1 bits 29:20 (USB PD R3.3 Table 6.8).
    return TPS25751Pdo(static_cast<uint32_t>(rawFirstPdoControlBits()) << 20, 1, PdoRole::Source);
}

bool TPS25751ActivePDOContract::hasActiveContract() const {
    // If all bits are zero, no contract exists
    return rawActivePdo() != 0;
}

void TPS25751ActivePDOContract::debugPrint(Stream &s) const {
    s.println(F("=== Active PDO Contract Register (0x34) ==="));

    if (!hasActiveContract()) {
        s.println(F("  No active PD contract"));
        s.println();
        return;
    }

    s.print(F("  Active PDO: 0x")); printHex(s, rawActivePdo(), 8); s.println();
    activePdo().debugPrint(s, "  ");

    const TPS25751Pdo first = firstPdoControl();
    s.print(F("  PDO 1 flags (First PDO Control Bits 0x"));
    printHex(s, rawFirstPdoControlBits(), 3);
    s.println(F("):"));
    s.print(F("    Dual-Role Power: ")); s.println(yesNo(first.dualRolePower()));
    s.print(F("    USB Suspend Supported: ")); s.println(yesNo(first.usbSuspendSupported()));
    s.print(F("    Unconstrained Power: ")); s.println(yesNo(first.unconstrainedPower()));
    s.print(F("    USB Communications Capable: ")); s.println(yesNo(first.usbCommunicationsCapable()));
    s.print(F("    Dual-Role Data: ")); s.println(yesNo(first.dualRoleData()));
    s.print(F("    Unchunked Extended Messages: ")); s.println(yesNo(first.unchunkedExtendedMessages()));
    s.print(F("    EPR Capable: ")); s.println(yesNo(first.eprCapable()));
    s.print(F("    PDO 1 Peak Current: ")); s.print(toString(first.peakCurrent()));
    s.print(F(" (")); s.print(first.rawPeakCurrent()); s.println(F(")"));

    s.println();
}

bool TPS25751ActivePDOContract::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT.size;
    if (!isValidSize(expectedSize)) return false;

    // If no active contract, that's valid
    if (!hasActiveContract()) return true;

    // The active PDO must pass the receiver rules of USB PD R3.3
    return activePdo().isValidForReceiver();
}
