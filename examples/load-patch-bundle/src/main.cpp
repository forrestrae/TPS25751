#include <Arduino.h>
#include <Wire.h>
#include <TPS25751.h>
#include <TPS25751PatchLoader.h>

#include "patch_bundle.h"  // tps25751_lowRegion_i2c_array / SIZEOFLRB (placeholder)

// ---------------------------------------------------------------------------
// Patch bundle loader example — EEPROM-less PTCH → APP bring-up path
//
// Demonstrates loading a TI patch bundle directly into the TPS25751 over the
// I2Ct bus when no external EEPROM is present (Patch Burst Mode, SLVAFV8A).
//
// Expected serial output on success:
//   [setup] TPS25751 patch loader example
//   [setup] Waiting for serial...
//   [setup] Wire started
//   [loadPatch] Starting PBM sequence (image: 1 bytes)   ← placeholder size
//   [loadPatch] Result: NotInPatchMode                   ← placeholder: device
//                                                          is in APP if EEPROM
//                                                          present, or Success
//                                                          with real bundle
//   [loadPatch] MODE after load: APP (or PTCH on failure)
//
// HARDWARE:
//   - Teensy 4.x MCU
//   - TPS25751 on Wire (I2C pins 18/19, address 0x20)
//   - I2Ct_IRQ wired to I2CT_IRQ_PIN (or -1 for pure-polling mode)
//   - No EEPROM: the TPS25751 must be in PTCH mode at power-on
//
// BEFORE USE:
//   Replace patch_bundle.h with the real low-region C array from TI's
//   USBCPD Application Customization Tool (see patch_bundle.h comments).
// ---------------------------------------------------------------------------

// GPIO pin connected to TPS25751 I2Ct_IRQ (active-low).
// Set to -1 to disable IRQ-wait and use pure COMMAND polling instead.
constexpr int I2CT_IRQ_PIN = -1;

TPS25751 pd(Wire, TPS25751_I2CT_ADDRESS_IDX[0]);
TPS25751PatchLoader loader(pd, I2CT_IRQ_PIN);

// ---------------------------------------------------------------------------

static void printMode()
{
    auto mode = pd.readModeRegister(/*validate=*/false);
    if (mode) {
        Serial.print(F("[mode] MODE = "));
        Serial.println(mode->modeString());
    } else {
        Serial.println(F("[mode] Failed to read MODE register"));
    }
}

static void loadPatch()
{
    Serial.print(F("[loadPatch] Starting PBM sequence (image: "));
    Serial.print(SIZEOFLRB);
    Serial.println(F(" bytes)"));

    PatchLoadStatus result = loader.loadPatchBundle(
        tps25751_lowRegion_i2c_array, SIZEOFLRB);

    Serial.print(F("[loadPatch] Result: "));
    Serial.println(TPS25751PatchLoader::toString(result));

    if (result == PatchLoadStatus::Success) {
        Serial.println(F("[loadPatch] Patch loaded successfully — device in APP mode"));
    } else {
        Serial.println(F("[loadPatch] Patch load failed — see result above"));
        Serial.println(F("[loadPatch] Check: device in PTCH mode? Bundle valid? Wiring?"));
    }

    Serial.print(F("[loadPatch] MODE after load: "));
    printMode();
}

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);

    Serial.println(F("[setup] TPS25751 patch loader example"));
    Serial.println(F("[setup] Waiting for serial..."));
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    // Optional: enable debug output to diagnose failures.
    // TPS25751::setDebugLevel(DEBUG_LEVEL_INFO);
    // TPS25751::setDebugCategories(DEBUG_CAT_TASK | DEBUG_CAT_I2C);

    if (I2CT_IRQ_PIN >= 0) {
        pinMode(I2CT_IRQ_PIN, INPUT_PULLUP);
    }

    Wire.begin();
    Wire.setClock(400000);
    pd.begin();

    Serial.println(F("[setup] Wire started"));

    loadPatch();
}

void loop()
{
    // Nothing after the one-shot load attempt.
    // Add your application logic here once the device is in APP mode.
    delay(5000);
    Serial.println(F("[loop] Patch load already attempted in setup(). Current mode:"));
    printMode();
}
