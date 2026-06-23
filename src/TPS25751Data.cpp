#include "TPS25751Data.h"

uint8_t TPS25751Data::byteAt(size_t index) const {
    if (!isValid() || index >= _len) return 0;
    return _raw[index];
}

void TPS25751Data::debugPrint(Stream &s) const {
    s.println(F("=== Data Register (0x09) ==="));

    if (!isValid()) {
        s.println(F("  <invalid>"));
        s.println();
        return;
    }

    s.print(F("  Length: "));
    s.print(_len);
    s.println(F(" bytes"));

    debugDumpRaw(s, "  Raw");

    s.println();
}

bool TPS25751Data::isSemanticallyValid() const {
    if (!isValid()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::DATA.size;
    return isValidSize(expectedSize);
}
