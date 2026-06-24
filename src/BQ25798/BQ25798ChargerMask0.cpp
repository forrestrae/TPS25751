#include "BQ25798/BQ25798ChargerMask0.h"

namespace BQ25798 {

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool ChargerMask0::iindpmMask()      const { return extractBits(7, 1) != 0; }
bool ChargerMask0::vindpmMask()      const { return extractBits(6, 1) != 0; }
bool ChargerMask0::wdMask()          const { return extractBits(5, 1) != 0; }
bool ChargerMask0::poorsrcMask()     const { return extractBits(4, 1) != 0; }
bool ChargerMask0::pgMask()          const { return extractBits(3, 1) != 0; }
bool ChargerMask0::ac2PresentMask()  const { return extractBits(2, 1) != 0; }
bool ChargerMask0::ac1PresentMask()  const { return extractBits(1, 1) != 0; }
bool ChargerMask0::vbusPresentMask() const { return extractBits(0, 1) != 0; }

// ---------------------------------------------------------------------------
// debugPrint
// ---------------------------------------------------------------------------

void ChargerMask0::debugPrint(Stream& s) const
{
    s.println(F("=== BQ25798 Charger Mask 0 Register (REG28h) ==="));

    if (!isValid()) {
        s.println(F("  [invalid — no data]"));
        return;
    }

    s.print(F("  IINDPM_MASK       (bit 7): "));
    s.println(iindpmMask()      ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  VINDPM_MASK       (bit 6): "));
    s.println(vindpmMask()      ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  WD_MASK           (bit 5): "));
    s.println(wdMask()          ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  POORSRC_MASK      (bit 4): "));
    s.println(poorsrcMask()     ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  PG_MASK           (bit 3): "));
    s.println(pgMask()          ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  AC2_PRESENT_MASK  (bit 2): "));
    s.println(ac2PresentMask()  ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  AC1_PRESENT_MASK  (bit 1): "));
    s.println(ac1PresentMask()  ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));
    s.print(F("  VBUS_PRESENT_MASK (bit 0): "));
    s.println(vbusPresentMask() ? F("Masked (no INT)") : F("Unmasked (INT enabled)"));

    s.print(F("  Raw byte: 0x"));
    if (_raw[0] < 0x10) s.print(F("0"));
    s.println(_raw[0], HEX);

    s.println();
}

// ---------------------------------------------------------------------------
// isSemanticallyValid
// ---------------------------------------------------------------------------

bool ChargerMask0::isSemanticallyValid() const
{
    if (!isValid()) return false;
    if (!isValidSize(kExpectedSize)) return false;
    return true;
}

}  // namespace BQ25798
