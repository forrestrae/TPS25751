#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <TPS25751.h>  // also brings in the EEPROM updater and TPS25751AppConfigImage

// The bundle is shared with the eeprom-update example: platformio.ini puts
// examples/eeprom-update/src on this environment's include path, so one
// scripts/bin_to_header.py run feeds both examples.
#include "patch_bundle.h"  // tps25751_eeprom_bundle / tps25751_eeprom_bundle_size

// ---------------------------------------------------------------------------
// Unattended EEPROM update
//
// Brings the TPS25751's configuration EEPROM to the embedded patch bundle once
// at power-up, with no serial input:
//
//   1. Wait up to BOOT_WAIT_MS for MODE to read APP.
//   2. Copy the bundle to RAM and apply applyEdits() (none by default) through
//      TPS25751AppConfigImage, which rewrites the app config CRC.
//   3. Compare the region the controller boots from with that image. When they
//      match, stop: nothing is written, so a power cycle costs no EEPROM wear.
//      The compare reads the whole region through FLrd: about 900 tasks and
//      16 s for a 14 KB bundle on the development board (eeprom-update timing).
//   4. Otherwise updateInactiveRegion(): write the other region, FLvy and an
//      FLrd compare, switch the pointers. A failure before the switch leaves
//      the old region booting.
//   5. coldReset(), then check that BOOT_FLAGS shows the new region loaded
//      with no Invalid or CRC flag and that VERSION matches the image.
//
// Status is printed on Serial at 115200 baud and shown on LED_BUILTIN:
//   on            the EEPROM holds the image (already, or after the update)
//   slow blink    nothing to do with this build: the placeholder bundle, or an
//                 edit that does not apply
//   fast blink    the update or the check after it failed; see the log
//
// THIS SKETCH WRITES THE EEPROM WITHOUT ASKING. Flash it only on a board whose
// configuration you mean to replace with the embedded bundle. The interactive
// eeprom-update example inspects, updates with a typed confirmation, and
// recovers a board that no longer boots; this one does not recover: with no
// bootable region the controller stays in PTCH and this sketch stops.
//
// HARDWARE:
//   - TPS25751 on Wire (address index 0) with a configuration EEPROM on I2Cc.
//
// BEFORE USE:
//   scripts/bin_to_header.py <build>_lowRegion.bin -o examples/eeprom-update/src/patch_bundle.h
// While the placeholder is in place the sketch writes nothing.
// ---------------------------------------------------------------------------

/// Longest wait for MODE to read APP after power-up. A choice, as in eeprom-update.
constexpr uint32_t BOOT_WAIT_MS = 5000;
constexpr uint32_t BOOT_POLL_MS = 10;

TPS25751 pd(Wire, TPS25751_I2CT_ADDRESS_IDX[0]);
TPS25751HostExecutor exec(pd);
TPS25751EepromUpdater updater(exec);

/// The image written: the bundle with applyEdits() applied. 14 KB of RAM for a
/// typical low region.
static uint8_t image[tps25751_eeprom_bundle_size];

enum class Outcome : uint8_t { Current, Updated, NothingToDo, Failed };

// ---------------------------------------------------------------------------
// Configuration edits
//
// Return false to stop without writing. Each edit replaces a whole register
// record; TPS25751AppConfigImage keeps the block length and CRC correct.
// Example, a 1000 ms sleep time:
//
//   auto sleep = img.recordAs<TPS25751SleepControl>(TPS25751Registers::Address::SLEEP_CONTROL);
//   if (!sleep) return false;
//   sleep->setSleepTime(SleepTime::Time1000ms);
//   return img.setRecord(*sleep) == TPS25751AppConfigStatus::Ok;
// ---------------------------------------------------------------------------

static bool applyEdits(TPS25751AppConfigImage& img)
{
    (void)img;
    return true;
}

// ---------------------------------------------------------------------------

static void printHex32(uint32_t v)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(v));
    Serial.print(buf);
}

static void printStatus(const __FlashStringHelper* what, EepromUpdateStatus s)
{
    Serial.print(what);
    Serial.print(TPS25751EepromUpdater::toString(s));
    Serial.print(F(" (last step "));
    Serial.print(TPS25751EepromUpdater::toString(updater.lastStep()));
    Serial.println(F(")"));
}

static bool waitForApp()
{
    TPS25751::setDebugLevel(DEBUG_LEVEL_NONE);  // the controller can NAK while it boots
    const uint32_t started = millis();
    bool app = false;
    while (!app && millis() - started < BOOT_WAIT_MS) {
        TPS25751Mode mode;
        app = pd.readMode(mode) && mode.isApplicationMode();
        if (!app) delay(BOOT_POLL_MS);
    }
    TPS25751::setDebugLevel(DEBUG_LEVEL_ERROR);
    return app;
}

/// The region the controller boots: Low when its pointer is valid (it is tried
/// first), else High. False when neither pointer is valid or one cannot be read.
static bool bootRegion(EepromRegion& out)
{
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        uint32_t value = 0;
        const EepromUpdateStatus s = updater.readRegionPointer(r, value);
        if (s != EepromUpdateStatus::Success) {
            printStatus(F("[auto] Region pointer read failed: "), s);
            return false;
        }
        if (value == TPS25751EepromUpdater::bundleStart(r)) {
            out = r;
            return true;
        }
    }
    Serial.println(F("[auto] Neither region pointer is valid."));
    return false;
}

/// BOOT_FLAGS and VERSION after the restart: the target region loaded cleanly.
static bool checkBoot(EepromRegion target)
{
    TPS25751BootFlags flags;
    TPS25751Version version;
    if (!pd.readBootFlags(flags) || !pd.readVersion(version)) {
        Serial.println(F("[auto] BOOT_FLAGS or VERSION read failed after the restart."));
        return false;
    }
    const bool low = (target == EepromRegion::Low);
    const bool attempted = low ? flags.region0Attempted() : flags.region1Attempted();
    const bool invalid = low ? flags.region0Invalid() : flags.region1Invalid();
    const bool crcFail = low ? flags.region0CrcFail() : flags.region1CrcFail();
    const bool versionOk = version.rawVersion() == TPS25751EepromUpdater::imageVersion(image, sizeof(image));

    Serial.print(F("[auto] After restart: VERSION "));
    printHex32(version.rawVersion());
    Serial.print(F(", source "));
    Serial.print(toString(flags.patchConfigSource()));
    Serial.print(F(", target attempted "));
    Serial.print(attempted ? F("yes") : F("no"));
    Serial.print(F(", invalid "));
    Serial.print(invalid ? F("yes") : F("no"));
    Serial.print(F(", CRC fail "));
    Serial.println(crcFail ? F("yes") : F("no"));
    return attempted && !invalid && !crcFail && versionOk &&
           flags.patchConfigSource() == PatchConfigSource::EEPROMLoaded;
}

static Outcome run()
{
    Serial.println(F("[auto] TPS25751 unattended EEPROM update"));
    if (!waitForApp()) {
        Serial.println(F("[auto] MODE did not read APP. A controller in PTCH has no bundle it can boot;"));
        Serial.println(F("[auto] recover it with the eeprom-update example (l, then pb)."));
        return Outcome::Failed;
    }

    EepromUpdateStatus s = TPS25751EepromUpdater::checkImage(
        tps25751_eeprom_bundle, tps25751_eeprom_bundle_size,
        updater.regionCapacity(EepromRegion::Low));
    if (s != EepromUpdateStatus::Success) {
        printStatus(F("[auto] The embedded bundle is not usable, nothing written: "), s);
        Serial.println(F("[auto] Replace the placeholder patch_bundle.h with scripts/bin_to_header.py."));
        return Outcome::NothingToDo;
    }

    memcpy(image, tps25751_eeprom_bundle, sizeof(image));
    TPS25751AppConfigImage img(image, sizeof(image));
    TPS25751AppConfigStatus a = img.check();
    if (a == TPS25751AppConfigStatus::Ok && !applyEdits(img)) {
        Serial.println(F("[auto] applyEdits() declined; nothing written."));
        return Outcome::NothingToDo;
    }
    a = img.check();
    if (a != TPS25751AppConfigStatus::Ok) {
        Serial.print(F("[auto] App config block: "));
        Serial.print(toString(a));
        Serial.println(F("; nothing written."));
        return Outcome::NothingToDo;
    }
    Serial.print(F("[auto] Image: "));
    Serial.print(sizeof(image));
    Serial.print(F(" bytes, version "));
    printHex32(TPS25751EepromUpdater::imageVersion(image, sizeof(image)));
    Serial.print(F(", app config CRC "));
    printHex32(img.storedCrc());
    Serial.println();

    EepromRegion booted;
    if (!bootRegion(booted)) return Outcome::Failed;
    s = updater.compareRegion(booted, image, sizeof(image));
    if (s == EepromUpdateStatus::Success) {
        Serial.print(F("[auto] The "));
        Serial.print(TPS25751EepromUpdater::toString(booted));
        Serial.println(F(" region already holds the image. Nothing written."));
        return Outcome::Current;
    }
    if (s != EepromUpdateStatus::CompareMismatch) {
        printStatus(F("[auto] Compare failed: "), s);
        return Outcome::Failed;
    }

    Serial.print(F("[auto] The "));
    Serial.print(TPS25751EepromUpdater::toString(booted));
    Serial.println(F(" region differs. Updating the other region..."));
    const uint32_t started = millis();
    s = updater.updateInactiveRegion(image, sizeof(image));
    if (s != EepromUpdateStatus::Success) {
        printStatus(F("[auto] Update failed: "), s);
        return Outcome::Failed;
    }
    const EepromRegion target = updater.lastTarget();
    Serial.print(F("[auto] Wrote and verified the "));
    Serial.print(TPS25751EepromUpdater::toString(target));
    Serial.print(F(" region in "));
    Serial.print(millis() - started);
    Serial.println(F(" ms. Restarting the controller; the port disconnects."));

    s = updater.coldReset();
    if (s != EepromUpdateStatus::Success && s != EepromUpdateStatus::ResetNotObserved) {
        printStatus(F("[auto] Restart failed: "), s);
        return Outcome::Failed;
    }
    if (!checkBoot(target)) {
        Serial.println(F("[auto] The controller did not boot the new region cleanly."));
        return Outcome::Failed;
    }
    Serial.println(F("[auto] Done: the controller booted the new image."));
    return Outcome::Updated;
}

// ---------------------------------------------------------------------------

static Outcome outcome = Outcome::Failed;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }
    Wire.begin();
    Wire.setClock(400000);
    pd.begin();

    outcome = run();
}

void loop()
{
    switch (outcome) {
        case Outcome::Current:
        case Outcome::Updated:
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            break;
        case Outcome::NothingToDo:
            digitalWrite(LED_BUILTIN, (millis() / 1000) % 2);
            delay(10);
            break;
        case Outcome::Failed:
            digitalWrite(LED_BUILTIN, (millis() / 100) % 2);
            delay(10);
            break;
    }
}
