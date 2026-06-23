#include <cstring>

#include "TPS25751Command.h"

const char* TPS25751Command::fourCC() const {
    constexpr size_t cmdSize = TPS25751Registers::Registers::COMMAND.size;
    static char cmdStr[cmdSize + 1];

    if (!isValid()) {
        cmdStr[0] = '\0';
        return cmdStr;
    }

    memcpy(cmdStr, _raw, cmdSize);
    cmdStr[cmdSize] = '\0';
    return cmdStr;
}

bool TPS25751Command::isClear() const {
    if (!isValid()) return false;

    for (size_t i = 0; i < _len; i++) {
        if (_raw[i] != 0) return false;
    }
    return true;
}

bool TPS25751Command::isRejected() const {
    if (!isValid()) return false;

    constexpr size_t cmdSize = TPS25751Registers::Registers::COMMAND.size;
    constexpr char REJECTED[] = "!CMD";
    static_assert(sizeof(REJECTED) - 1 == cmdSize, "!CMD string must match register size");

    return _len == cmdSize && memcmp(_raw, REJECTED, cmdSize) == 0;
}

void TPS25751Command::debugPrint(Stream &s) const {
    s.println(F("=== Command Register (0x08) ==="));

    if (!isValid()) {
        s.println(F("  <invalid>"));
        s.println();
        return;
    }

    s.print(F("  Raw 4CC: ")); s.println(fourCC());
    s.print(F("  State: "));
    if (isClear()) {
        s.println(F("Clear (no command pending / last command succeeded)"));
    } else if (isRejected()) {
        s.println(F("Rejected (\"!CMD\" - unrecognized task code)"));
    } else {
        s.println(F("Pending / unrecognized echo"));
    }

    s.println();
}

bool TPS25751Command::isSemanticallyValid() const {
    if (!isValid()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::COMMAND.size;
    if (!isValidSize(expectedSize)) return false;

    // Clear (all-zero) and the rejection code are always valid.
    if (isClear() || isRejected()) return true;

    // Otherwise, a valid 4CC task code must be printable ASCII.
    for (size_t i = 0; i < _len; i++) {
        if (_raw[i] < 0x20 || _raw[i] > 0x7E) {
            return false;
        }
    }

    return true;
}
