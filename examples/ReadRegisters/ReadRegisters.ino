/*
 * ReadRegisters - TPS25751 Register Dump Example
 *
 * Reads all implemented TPS25751 USB-C PD controller registers every 5 seconds
 * and prints human-readable output over Serial at 115200 baud.
 *
 * Hardware:
 *   - Teensy 4.0 or 4.1
 *   - TPS25751 connected via I2C (default SDA/SCL pins)
 *   - TPS25751 INT pin connected to I2CT_IRQ_PIN (pin 17 by default)
 *
 * This example is licensed under the Polyform Noncommercial License 1.0.0.
 * Copyright (c) 2025 Forrest Rae
 */

#include <TPS25751.h>
#include <TPS25751Status.h>
#include <TPS25751Mode.h>
#include <TPS25751PowerPathStatus.h>
#include <TPS25751BootFlags.h>
#include <TPS25751PortConfig.h>
#include <TPS25751InterruptEvent.h>

// Change to match your interrupt pin wiring
#define I2CT_IRQ_PIN 17

const TPS25751 pd;

// Called when TPS25751 asserts its active-low interrupt line
void handleInterruptEvent()
{
    if (auto interruptEvent = pd.readInterruptEventRegister(true))
        interruptEvent->debugPrint();
    else
        Serial.println(F("Failed to read Interrupt Event register"));
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
        delay(10);

    // Set debug verbosity and categories
    TPS25751::setDebugLevel(DEBUG_LEVEL_WARN);
    TPS25751::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_REGISTER | DEBUG_CAT_VALIDATION);

    // Attach interrupt handler
    pinMode(I2CT_IRQ_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(I2CT_IRQ_PIN), handleInterruptEvent, FALLING);

    Serial.println(F("Initializing TPS25751..."));
    pd.begin();
}

void printStatus()
{
    Serial.println(F("\n-------- STATUS --------"));

    if (auto modeReg = pd.readModeRegister(true))
    {
        modeReg->debugPrint();
        if (!modeReg->isApplicationMode())
        {
            Serial.println(F("Not in Application mode — rebooting in 10 s"));
            delay(10000);
        }
    }
    else
    {
        Serial.println(F("Failed to read Mode register"));
    }

    if (auto bootFlags = pd.readBootFlagsRegister(true))
        bootFlags->debugPrint();
    else
        Serial.println(F("Failed to read Boot Flags register"));

    if (auto status = pd.readStatusRegister(true))
        status->debugPrint();
    else
        Serial.println(F("Failed to read Status register"));

    if (auto pdStatus = pd.readPDStatusRegister(true))
        pdStatus->debugPrint();
    else
        Serial.println(F("Failed to read PD Status register"));
}

void printPowerStatus()
{
    Serial.println(F("\n-------- POWER --------"));

    if (auto powerStatus = pd.readPowerStatusRegister(true))
        powerStatus->debugPrint();
    else
        Serial.println(F("Failed to read Power Status register"));

    if (auto powerPath = pd.readPowerPathStatusRegister(true))
        powerPath->debugPrint();
    else
        Serial.println(F("Failed to read Power Path Status register"));
}

void printPortStatus()
{
    Serial.println(F("\n-------- PORT --------"));

    if (auto portConfig = pd.readPortConfigRegister(true))
        portConfig->debugPrint();
    else
        Serial.println(F("Failed to read Port Config register"));

    if (auto portControl = pd.readPortControlRegister(true))
        portControl->debugPrint();
    else
        Serial.println(F("Failed to read Port Control register"));

    if (auto typeCState = pd.readTypeCStateRegister(true))
        typeCState->debugPrint();
    else
        Serial.println(F("Failed to read Type-C State register"));
}

void printContracts()
{
    Serial.println(F("\n-------- CONTRACTS --------"));

    if (auto sourceCaps = pd.readReceivedSourceCapsRegister(true))
        sourceCaps->debugPrint();
    else
        Serial.println(F("Failed to read Received Source Caps register"));

    if (auto sinkCaps = pd.readReceivedSinkCapsRegister(true))
        sinkCaps->debugPrint();
    else
        Serial.println(F("Failed to read Received Sink Caps register"));

    if (auto pdoContract = pd.readActivePDOContractRegister(true))
        pdoContract->debugPrint();
    else
        Serial.println(F("Failed to read Active PDO Contract register"));

    if (auto rdoContract = pd.readActiveRDOContractRegister(true))
        rdoContract->debugPrint();
    else
        Serial.println(F("Failed to read Active RDO Contract register"));

    if (auto gpioStatus = pd.readGPIOStatusRegister(true))
        gpioStatus->debugPrint();
    else
        Serial.println(F("Failed to read GPIO Status register"));
}

void loop()
{
    Serial.println(F("\n========================================"));
    Serial.print(F("Uptime: "));
    Serial.print(millis());
    Serial.println(F(" ms"));
    Serial.println(F("========================================"));

    printStatus();
    printPowerStatus();
    printPortStatus();
    printContracts();

    delay(5000);
}
