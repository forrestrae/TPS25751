#include "TPS25751ActiveRDOContract.h"

uint8_t TPS25751ActiveRDOContract::objectPosition() const {
    return extractBits(28, 4);  // Bits 31:28
}

bool TPS25751ActiveRDOContract::giveBackFlag() const {
    return extractBits(27, 1);
}

bool TPS25751ActiveRDOContract::capabilityMismatch() const {
    return extractBits(26, 1);
}

bool TPS25751ActiveRDOContract::usbCommCapable() const {
    return extractBits(25, 1);
}

bool TPS25751ActiveRDOContract::noUSBSuspend() const {
    return extractBits(24, 1);
}

bool TPS25751ActiveRDOContract::unchunkedSupported() const {
    return extractBits(23, 1);
}

uint16_t TPS25751ActiveRDOContract::operatingCurrent() const {
    return extractBits(10, 10);  // Bits 19:10
}

uint16_t TPS25751ActiveRDOContract::operatingCurrent_mA() const {
    return operatingCurrent() * 10;  // 10mA units
}

uint16_t TPS25751ActiveRDOContract::maxMinCurrent() const {
    return extractBits(0, 10);  // Bits 9:0
}

uint16_t TPS25751ActiveRDOContract::maxMinCurrent_mA() const {
    return maxMinCurrent() * 10;  // 10mA units
}

bool TPS25751ActiveRDOContract::hasActiveContract() const {
    // If all bits are zero, no contract exists
    uint32_t allBits = extractBits(0, 32);
    return allBits != 0;
}

void TPS25751ActiveRDOContract::debugPrint(Stream &s) const {
    s.println(F("=== Active RDO Contract Register (0x35) ==="));

    if (!hasActiveContract()) {
        s.println(F("  No active RDO contract"));
        s.println();
        return;
    }

    s.print(F("  Object Position: ")); s.println(objectPosition());
    s.print(F("  GiveBack Flag: ")); s.println(giveBackFlag() ? F("Yes") : F("No"));
    s.print(F("  Capability Mismatch: ")); s.println(capabilityMismatch() ? F("Yes") : F("No"));
    s.print(F("  USB Comm Capable: ")); s.println(usbCommCapable() ? F("Yes") : F("No"));
    s.print(F("  No USB Suspend: ")); s.println(noUSBSuspend() ? F("Yes") : F("No"));
    s.print(F("  Unchunked Supported: ")); s.println(unchunkedSupported() ? F("Yes") : F("No"));
    s.print(F("  Operating Current: ")); s.print(operatingCurrent_mA()); s.println(F(" mA"));
    s.print(F("  Max/Min Current: ")); s.print(maxMinCurrent_mA()); s.println(F(" mA"));

    s.println();
}

bool TPS25751ActiveRDOContract::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT.size;
    if (!isValidSize(expectedSize)) return false;

    // If no active contract, that's valid
    if (!hasActiveContract()) return true;

    // Object position should be 1-7 (PDO index)
    uint8_t objPos = objectPosition();
    if (objPos < 1 || objPos > 7) {
        return false;
    }

    // Operating current and max/min should be non-zero for real contract
    uint16_t opCurrent = operatingCurrent();
    uint16_t maxMin = maxMinCurrent();

    if (opCurrent == 0 && maxMin == 0) {
        // Both zero with non-zero contract is suspicious
        return false;
    }

    // For GiveBack, max/min should be less than or equal to operating
    if (giveBackFlag()) {
        if (maxMin > opCurrent) {
            return false;  // GiveBack min shouldn't exceed operating
        }
    }

    return true;
}
