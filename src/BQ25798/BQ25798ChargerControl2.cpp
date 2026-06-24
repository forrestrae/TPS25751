#include "BQ25798/BQ25798ChargerControl2.h"

namespace BQ25798 {

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool ChargerControl2::forceIndet() const
{
    // FORCE_INDET — bit 7 of byte 0
    return extractBits(7, 1) != 0;
}

bool ChargerControl2::autoIndetEn() const
{
    // AUTO_INDET_EN — bit 6 of byte 0
    return extractBits(6, 1) != 0;
}

bool ChargerControl2::en12v() const
{
    // EN_12V — bit 5 of byte 0
    return extractBits(5, 1) != 0;
}

bool ChargerControl2::en9v() const
{
    // EN_9V — bit 4 of byte 0
    return extractBits(4, 1) != 0;
}

bool ChargerControl2::hvdcpEn() const
{
    // HVDCP_EN — bit 3 of byte 0
    return extractBits(3, 1) != 0;
}

ChargerControl2::SdrvCtrl ChargerControl2::sdrvCtrl() const
{
    // SDRV_CTRL_1:0 — 2-bit field, bits 2:1 (bit position 1, width 2)
    return extractEnum<SdrvCtrl>(
        1, 2,
        static_cast<uint8_t>(SdrvCtrl::SystemPowerReset),  // max valid = 3
        SdrvCtrl::Idle                                      // default on out-of-range
    );
}

bool ChargerControl2::sdrvDly() const
{
    // SDRV_DLY — bit 0 of byte 0 (1 = no delay, 0 = 10 s delay)
    return extractBits(0, 1) != 0;
}

// ---------------------------------------------------------------------------
// debugPrint
// ---------------------------------------------------------------------------

void ChargerControl2::debugPrint(Stream& s) const
{
    s.println(F("=== BQ25798 Charger Control 2 Register (REG11h) ==="));

    if (!isValid()) {
        s.println(F("  [invalid — no data]"));
        return;
    }

    s.print(F("  FORCE_INDET    (bit 7): "));
    s.println(forceIndet() ? F("Force D+/D- detection") : F("Normal"));

    s.print(F("  AUTO_INDET_EN  (bit 6): "));
    s.println(autoIndetEn() ? F("Auto detection enabled (default)") : F("Auto detection disabled"));

    s.print(F("  EN_12V         (bit 5): "));
    s.println(en12v() ? F("12 V HVDCP enabled") : F("12 V HVDCP disabled (default)"));

    s.print(F("  EN_9V          (bit 4): "));
    s.println(en9v() ? F("9 V HVDCP enabled") : F("9 V HVDCP disabled (default)"));

    s.print(F("  HVDCP_EN       (bit 3): "));
    s.println(hvdcpEn() ? F("HVDCP handshake enabled") : F("HVDCP handshake disabled (default)"));

    s.print(F("  SDRV_CTRL      (bits 2:1): "));
    switch (sdrvCtrl()) {
        case SdrvCtrl::Idle:             s.println(F("IDLE (default)"));      break;
        case SdrvCtrl::ShutdownMode:     s.println(F("Shutdown Mode"));       break;
        case SdrvCtrl::ShipMode:         s.println(F("Ship Mode"));           break;
        case SdrvCtrl::SystemPowerReset: s.println(F("System Power Reset")); break;
        default:                         s.println(F("Unknown"));              break;
    }

    s.print(F("  SDRV_DLY       (bit 0): "));
    s.println(sdrvDly() ? F("No 10 s delay") : F("10 s delay (default)"));

    s.print(F("  Raw byte: 0x"));
    if (_raw[0] < 0x10) s.print(F("0"));
    s.println(_raw[0], HEX);

    s.println();
}

// ---------------------------------------------------------------------------
// isSemanticallyValid
// ---------------------------------------------------------------------------

bool ChargerControl2::isSemanticallyValid() const
{
    // Tier 1: basic validity (non-null pointer, non-zero length)
    if (!isValid()) return false;

    // Tier 2: data validity — correct register size
    if (!isValidSize(kExpectedSize)) return false;

    // Tier 3: all bits defined — no additional semantic constraints
    return true;
}

}  // namespace BQ25798
