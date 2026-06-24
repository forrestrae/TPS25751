#include <Arduino.h>
#include <TPS25751.h>
#include <TPS25751DownstreamDevice.h>

// ---------------------------------------------------------------------------
// Minimal BQ25798 read sketch — validates the I2Cc downstream-device path
// (TPS25751DownstreamDevice / I2Cr) against a logic-analyzer capture.
//
// It reads the BQ25798 Part Information register (REG48h) once per loop, well
// over the TRM's 5 s minimum spacing apart, so each I2Cc transaction is cleanly
// isolated on the analyzer. Read-only / non-destructive — no I2Cw is issued.
//
// Wiring: BQ25798 sits on the TPS25751's secondary I2Cc bus (not the Teensy's
// primary I2Ct bus). Probe I2Cc_SDA/SCL with the analyzer.
// ---------------------------------------------------------------------------

// BQ25798: 7-bit I2C address 0x6B (do NOT include an R/W bit — the library
// masks bit 7, and the I2Cr/I2Cw DATA payload expects a 7-bit target address).
static constexpr uint8_t BQ25798_I2C_ADDR      = 0x6B;
// REG48h Part Information: PN[5:3], DEV_REV[2:0]. BQ25798 reads ~0x18/0x19
// (PN = 0b011). Good fixed-value target for verifying the read path.
static constexpr uint8_t BQ25798_REG_PART_INFO = 0x48;
static constexpr uint8_t BQ25798_PN_EXPECTED   = 0b011;

const TPS25751 pd;                                   // host on the primary I2Ct bus
TPS25751DownstreamDevice bq(pd, BQ25798_I2C_ADDR);   // proxy over I2Cr/I2Cw 4CC tasks

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
    {
        delay(10);
    }

    // Surface every I2Ct transaction and 4CC task step so the serial log lines
    // up 1:1 with what you see on the analyzer.
    TPS25751::setDebugLevel(DEBUG_LEVEL_INFO);
    TPS25751::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_TASK);

    Serial.println(F("Initializing TPS25751 host..."));
    pd.begin();

    Serial.println(F("BQ25798 downstream-read sketch ready."));
    Serial.println(F("One I2Cr per loop, >=6s apart, for clean logic-analyzer captures."));
}

void loop()
{
    Serial.println();
    Serial.println(F("---- I2Cr: BQ25798 REG48 (Part Information), 1 byte ----"));
    Serial.println(F("Expected I2Cc txn: [S] 0x6B+W, 0x48, [Sr] 0x6B+R, <1 byte>, [P]"));

    uint8_t partInfo = 0;
    if (bq.readRegister(BQ25798_REG_PART_INFO, &partInfo, 1))
    {
        const uint8_t pn  = (partInfo >> 3) & 0x07;  // PN[5:3]
        const uint8_t rev = partInfo & 0x07;         // DEV_REV[2:0]

        Serial.print(F("  REG48 = 0x"));       Serial.println(partInfo, HEX);
        Serial.print(F("  PN[5:3] = 0b"));     Serial.println(pn, BIN);
        Serial.print(F("  DEV_REV[2:0] = "));  Serial.println(rev);

        if (pn == BQ25798_PN_EXPECTED)
            Serial.println(F("  OK: PN=011b -> BQ25798 (data decoded from DATA offset 1 correctly)"));
        else
            Serial.println(F("  NOTE: PN != 011b -> wrong device, or a codec/DATA-offset mismatch"));
    }
    else
    {
        Serial.println(F("  FAILED: I2Cr did not complete (status or returnCode != success)"));
        Serial.println(F("  Check: device present @0x6B on I2Cc, bus pull-ups, and the offset-1 decode"));
    }

    // To also exercise length>1 / multi-byte offset handling, bump the count
    // (REG48..REG49 are valid), e.g.:
    //     uint8_t buf[2];
    //     bq.readRegister(BQ25798_REG_PART_INFO, buf, 2);

    // TRM requires >=5 s between consecutive I2Cr commands. The library only
    // warns (DEBUG_CAT_TASK), it does not block — so we pace it here.
    delay(6000);
}
