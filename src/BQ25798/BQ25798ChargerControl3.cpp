#include "BQ25798/BQ25798ChargerControl3.h"

namespace BQ25798 {

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool ChargerControl3::disAcdrv() const
{
    // DIS_ACDRV — bit 7 of byte 0
    return extractBits(7, 1) != 0;
}

bool ChargerControl3::enOtg() const
{
    // EN_OTG — bit 6 of byte 0
    return extractBits(6, 1) != 0;
}

bool ChargerControl3::pfmOtgDis() const
{
    // PFM_OTG_DIS — bit 5 of byte 0 (1 = PFM off)
    return extractBits(5, 1) != 0;
}

bool ChargerControl3::pfmFwdDis() const
{
    // PFM_FWD_DIS — bit 4 of byte 0 (1 = PFM off)
    return extractBits(4, 1) != 0;
}

bool ChargerControl3::wkupDly() const
{
    // WKUP_DLY — bit 3 of byte 0 (1 = 15 ms, 0 = 1 s)
    return extractBits(3, 1) != 0;
}

bool ChargerControl3::disLdo() const
{
    // DIS_LDO — bit 2 of byte 0 (1 = LDO off)
    return extractBits(2, 1) != 0;
}

bool ChargerControl3::disOtgOoa() const
{
    // DIS_OTG_OOA — bit 1 of byte 0 (1 = OOA off in OTG)
    return extractBits(1, 1) != 0;
}

bool ChargerControl3::disFwdOoa() const
{
    // DIS_FWD_OOA — bit 0 of byte 0 (1 = OOA off in forward mode)
    return extractBits(0, 1) != 0;
}

// ---------------------------------------------------------------------------
// debugPrint
// ---------------------------------------------------------------------------

void ChargerControl3::debugPrint(Stream& s) const
{
    s.println(F("=== BQ25798 Charger Control 3 Register (REG12h) ==="));

    if (!isValid()) {
        s.println(F("  [invalid — no data]"));
        return;
    }

    s.print(F("  DIS_ACDRV    (bit 7): "));
    s.println(disAcdrv() ? F("Both ACDRV drivers forced off") : F("Normal"));

    s.print(F("  EN_OTG       (bit 6): "));
    s.println(enOtg() ? F("OTG Enabled") : F("OTG Disabled (default)"));

    s.print(F("  PFM_OTG_DIS  (bit 5): "));
    s.println(pfmOtgDis() ? F("PFM off in OTG") : F("PFM on in OTG (default)"));

    s.print(F("  PFM_FWD_DIS  (bit 4): "));
    s.println(pfmFwdDis() ? F("PFM off in forward mode") : F("PFM on in forward mode (default)"));

    s.print(F("  WKUP_DLY     (bit 3): "));
    s.println(wkupDly() ? F("15 ms QON pull-low time") : F("1 s QON pull-low time (default)"));

    s.print(F("  DIS_LDO      (bit 2): "));
    s.println(disLdo() ? F("LDO off in pre-charge") : F("LDO on in pre-charge (default)"));

    s.print(F("  DIS_OTG_OOA  (bit 1): "));
    s.println(disOtgOoa() ? F("OOA off in OTG") : F("OOA on if PFM on (default)"));

    s.print(F("  DIS_FWD_OOA  (bit 0): "));
    s.println(disFwdOoa() ? F("OOA off in forward mode") : F("OOA on if PFM on (default)"));

    s.print(F("  Raw byte: 0x"));
    if (_raw[0] < 0x10) s.print(F("0"));
    s.println(_raw[0], HEX);

    s.println();
}

// ---------------------------------------------------------------------------
// isSemanticallyValid
// ---------------------------------------------------------------------------

bool ChargerControl3::isSemanticallyValid() const
{
    // Tier 1: basic validity (non-null pointer, non-zero length)
    if (!isValid()) return false;

    // Tier 2: data validity — correct register size
    if (!isValidSize(kExpectedSize)) return false;

    // Tier 3: all bits defined — no additional semantic constraints
    return true;
}

}  // namespace BQ25798
