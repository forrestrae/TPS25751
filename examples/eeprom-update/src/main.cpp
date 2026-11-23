#include <Arduino.h>
#include <Wire.h>
#include <TPS25751.h>  // also brings in TPS25751HostExecutor and TPS25751EepromUpdater

// ---------------------------------------------------------------------------
// EEPROM inspection example (read-only)
//
// Reads the TPS25751 configuration EEPROM through the flash 4CC tasks while the
// controller runs in APP mode, and prints what an update would start from:
//
//   - MODE, VERSION, and BOOT_FLAGS (0x2D; Patch Config Source and the region flags);
//   - both region pointers and whether each equals its bundle start;
//   - the region an update would target (TPS25751EepromUpdater::selectTargetRegion);
//   - the FLvy result for each region's bundle start;
//   - a hex dump of 0x0000 to 0x7C00 between two marker lines.
//
// It issues only FLrd and FLvy. It makes no EEPROM write and does not reset
// the controller.
//
// The report runs once in setup() and again each time "r" arrives over serial,
// so a monitor attached late can still capture a complete run.
//
// The dump lines have the form "xxxxxxxx: xx xx ... xx", which xxd -r turns
// back into binary. The BEGIN and END marker lines are for reading the log;
// they are not neutral to xxd -r, which would parse parts of them as an
// address and a byte. Never feed the whole log to xxd. Extract the dump lines
// first, from a log that holds exactly one report:
//
//   grep -aE '^[0-9a-f]{8}:( [0-9a-f]{2}){1,16}\r?$' serial.log > dump.txt
//   wc -l dump.txt                  # must print 1984
//   xxd -r dump.txt > eeprom.bin
//   cmp eeprom.bin <image>_fullFlash.bin
//
// A log with several reports gives a multiple of 1984 lines; keep one run.
//
// HARDWARE:
//   - Teensy 4.x MCU
//   - TPS25751 on Wire (I2C pins 18/19, address 0x20), in APP mode
//   - External EEPROM on the TPS25751's I2Cc bus (24LC256, 32 KB)
// ---------------------------------------------------------------------------

/// End of the dumped range: the size of the full-flash image (31744 bytes).
constexpr uint32_t DUMP_END = 0x7C00;

TPS25751 pd(Wire, TPS25751_I2CT_ADDRESS_IDX[0]);
TPS25751HostExecutor exec(pd);
TPS25751EepromUpdater updater(exec);

// ---------------------------------------------------------------------------

static void printHex32(uint32_t v)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(v));
    Serial.print(buf);
}

static bool printMode()
{
    TPS25751Mode mode;
    if (!pd.readMode(mode)) {
        Serial.println(F("[mode] Failed to read MODE"));
        return false;
    }
    Serial.print(F("[mode] MODE = '"));
    Serial.print(mode.modeString());
    Serial.println(F("'"));
    return mode.isApplicationMode();
}

static void printVersion()
{
    auto version = pd.readVersionRegister(/*validate=*/false);
    if (!version) {
        Serial.println(F("[version] Failed to read VERSION"));
        return;
    }
    Serial.print(F("[version] VERSION = "));
    printHex32(version->rawVersion());
    Serial.println();
}

static void printFlag(const __FlashStringHelper* name, bool value)
{
    Serial.print(F("[boot]   "));
    Serial.print(name);
    Serial.println(value ? F(": 1") : F(": 0"));
}

static void printBootFlags()
{
    TPS25751BootFlags flags;
    if (!pd.readBootFlags(flags)) {
        Serial.println(F("[boot] Failed to read BOOT_FLAGS"));
        return;
    }
    Serial.print(F("[boot] BOOT_FLAGS raw ="));
    for (size_t i = 0; i < flags.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), " %02X", flags.raw()[i]);
        Serial.print(buf);
    }
    Serial.println();
    Serial.print(F("[boot]   Patch Config Source: "));
    Serial.println(toString(flags.patchConfigSource()));
    printFlag(F("I2C EEPROM Present"), flags.i2cEepromPresent());
    printFlag(F("Region 0 Attempted"), flags.region0Attempted());
    printFlag(F("Region 1 Attempted"), flags.region1Attempted());
    printFlag(F("Region 0 Invalid"), flags.region0Invalid());
    printFlag(F("Region 1 Invalid"), flags.region1Invalid());
    printFlag(F("Region 0 EEPROM Error"), flags.region0EepromError());
    printFlag(F("Region 1 EEPROM Error"), flags.region1EepromError());
    printFlag(F("Region 0 CRC Fail"), flags.region0CrcFail());
    printFlag(F("Region 1 CRC Fail"), flags.region1CrcFail());
    printFlag(F("Patch Header Error"), flags.patchHeaderError());
}

static void printPointer(EepromRegion r)
{
    Serial.print(F("[pointer] "));
    Serial.print(TPS25751EepromUpdater::toString(r));
    Serial.print(F(" pointer at "));
    printHex32(TPS25751EepromUpdater::regionPointerAddress(r));
    Serial.print(F(" = "));

    uint32_t value = 0;
    const EepromUpdateStatus s = updater.readRegionPointer(r, value);
    if (s != EepromUpdateStatus::Success) {
        Serial.print(F("read failed: "));
        Serial.println(TPS25751EepromUpdater::toString(s));
        return;
    }
    printHex32(value);
    Serial.println(value == TPS25751EepromUpdater::bundleStart(r) ? F(" (valid)") : F(" (not valid)"));
}

static void printTarget()
{
    EepromRegion target = EepromRegion::Low;
    const EepromUpdateStatus s = updater.selectTargetRegion(target);
    Serial.print(F("[target] Update would write region: "));
    if (s == EepromUpdateStatus::Success) {
        Serial.println(TPS25751EepromUpdater::toString(target));
    } else {
        Serial.print(F("none ("));
        Serial.print(TPS25751EepromUpdater::toString(s));
        Serial.println(F(")"));
    }
}

static void printVerify(EepromRegion r)
{
    const uint32_t start = TPS25751EepromUpdater::bundleStart(r);
    const EepromUpdateStatus s = updater.verifyBundle(start);
    Serial.print(F("[verify] FLvy "));
    Serial.print(TPS25751EepromUpdater::toString(r));
    Serial.print(F(" bundle at "));
    printHex32(start);
    Serial.print(F(": "));
    Serial.println(TPS25751EepromUpdater::toString(s));
}

static void dumpEeprom()
{
    Serial.println(F("-----BEGIN EEPROM DUMP 0x00000000-0x00007C00-----"));
    const uint32_t started = millis();
    const EepromUpdateStatus s = updater.dump(0, DUMP_END, Serial);
    const uint32_t elapsed = millis() - started;
    Serial.println(F("-----END EEPROM DUMP-----"));

    Serial.print(F("[dump] Result: "));
    Serial.println(TPS25751EepromUpdater::toString(s));
    if (s != EepromUpdateStatus::Success) {
        Serial.println(F("[dump] Dump incomplete; the lines above stop at the failing read."));
        return;
    }
    Serial.print(F("[dump] "));
    Serial.print(DUMP_END / TPS25751EepromUpdater::kFlashReadBytes);
    Serial.print(F(" FLrd calls in "));
    Serial.print(elapsed);
    Serial.println(F(" ms"));
}

// Runs every read-only step once. Issues only FLrd and FLvy.
static void runReport()
{
    Serial.println(F("[report] TPS25751 EEPROM inspection (read-only)"));

    const bool app = printMode();
    printVersion();
    printBootFlags();

    if (!app) {
        Serial.println(F("[report] Not in APP mode; SLVAFL1 describes EEPROM access in APP mode. Skipping the flash tasks."));
    } else {
        printPointer(EepromRegion::Low);
        printPointer(EepromRegion::High);
        printTarget();
        printVerify(EepromRegion::Low);
        printVerify(EepromRegion::High);
        dumpEeprom();
        Serial.println(F("[report] Done. No EEPROM write was made."));
    }

    Serial.println(F("[hint] Send 'r' to run the read-only report again."));
}

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    // Errors only, so warnings cannot land inside the dump.
    TPS25751::setDebugLevel(DEBUG_LEVEL_ERROR);
    TPS25751::setDebugCategories(DEBUG_CAT_TASK | DEBUG_CAT_I2C);

    pd.begin();

    runReport();
}

void loop()
{
    while (Serial.available() > 0) {
        if (Serial.read() == 'r') {
            runReport();
        }
    }
    delay(10);
}
