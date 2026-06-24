#include <Arduino.h>
#include <TPS25751.h>
#include <TPS25751Status.h>
#include <TPS25751Mode.h>
#include <TPS25751PowerPathStatus.h>
#include <TPS25751BootFlags.h>
#include <TPS25751PortConfig.h>
#include <TPS25751RegisterFactory.h>
#include "TPS25751InterruptEvent.h"

#define I2CT_IRQ_PIN 17

const TPS25751 pd;

void doReboot()
{
    USB1_USBCMD = 0; // disconnect USB
    delay(50); // enough time for USB hubs/ports to detect disconnect
    SCB_AIRCR = 0x05FA0004;
}

void handleInterruptEvent()
{
    if (auto interruptEvent = pd.readInterruptEventRegister(true))
        interruptEvent->debugPrint();
    else
        Serial.println(F("Failed to create/read Interrupt Event register via factory"));
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
    {
        delay(10);
    }

    // Configure enhanced debugging
    TPS25751::setDebugLevel(DEBUG_LEVEL_WARN);
    TPS25751::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_REGISTER | DEBUG_CAT_VALIDATION);

    // Configure TPS25751 Interrupt Pin
    pinMode(I2CT_IRQ_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(I2CT_IRQ_PIN), handleInterruptEvent, FALLING);

    Serial.println(F("Initializing TPS25751..."));
    pd.begin();
}

void print_boot_flags()
{
    Serial.println();
    Serial.println("-------------------- print_boot_flags --------------------");
    Serial.println();

    if (auto bootFlagsReg = pd.readBootFlagsRegister(true))
        bootFlagsReg->debugPrint();
    else
        Serial.println(F("Failed to create/read Boot Flags register via factory"));
}

void print_status_register()
{
    Serial.println();
    Serial.println("-------------------- print_status --------------------");
    Serial.println();

    if (const auto statusReg = pd.readStatusRegister(true))
        statusReg->debugPrint();
    else
        Serial.println(F("Failed to create/read Status register via factory"));
}

void print_pd_status()
{
    Serial.println();
    Serial.println("-------------------- print_pd_status --------------------");
    Serial.println();

    if (auto pdStatusReg = pd.readPDStatusRegister(true))
        pdStatusReg->debugPrint();
    else
        Serial.println(F("Failed to create/read pdStatusReg register via factory"));
}

void print_power_status()
{
    Serial.println();
    Serial.println("-------------------- print_power_status --------------------");
    Serial.println();

    if (const auto powerStatus = pd.readPowerStatusRegister(true))
        powerStatus->debugPrint();
    else
        Serial.println(F("Failed to create/read Power Status register via factory"));

    if (auto powerPathStatuReg = pd.readPowerPathStatusRegister(true))
        powerPathStatuReg->debugPrint();
    else
        Serial.println(F("Failed to create/read powerPathStatuReg register via factory"));
}

void print_port_status()
{
    Serial.println();
    Serial.println("-------------------- print_port_status --------------------");
    Serial.println();

    if (auto portConfigReg = pd.readPortConfigRegister(true))
        portConfigReg->debugPrint();
    else
        Serial.println(F("Failed to create/read portConfigReg register via factory"));

    if (const auto portControl = pd.readPortControlRegister(true))
        portControl->debugPrint();
    else
        Serial.println(F("Failed to create/read Port Control register via factory"));

    if (const auto typeCStateReg = pd.readTypeCStateRegister(true))
        typeCStateReg->debugPrint();
    else
        Serial.println(F("Failed to create/read Type C State Reg register via factory"));
}

void print_contracts()
{
    Serial.println();
    Serial.println("-------------------- print_contracts --------------------");
    Serial.println();

    if (const auto sourceCaps = pd.readReceivedSourceCapsRegister(true))
        sourceCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read Received Source Capabilities register via factory"));


    if (const auto sinkCaps = pd.readReceivedSinkCapsRegister(true))
        sinkCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read Received Sink Capabilities register via factory"));


    if (const auto pdoContract = pd.readActivePDOContractRegister(true))
        pdoContract->debugPrint();
    else
        Serial.println(F("Failed to create/read Active PDO Contract register via factory"));

    if (const auto rdoContract = pd.readActiveRDOContractRegister(true))
        rdoContract->debugPrint();
    else
        Serial.println(F("Failed to create/read Active RDO Contract register via factory"));
}

void print_gpio_status()
{
    Serial.println();
    Serial.println("-------------------- print_gpio_status --------------------");
    Serial.println();

    if (const auto gpioStatus = pd.readGPIOStatusRegister(true))
        gpioStatus->debugPrint();
    else
        Serial.println(F("Failed to create/read GPIO Status register via factory"));
}

void print_app_mode_status(const std::unique_ptr<TPS25751Mode> &modeReg)
{
    modeReg->debugPrint();

    print_boot_flags();
    print_status_register();
    print_pd_status();
    print_power_status();
    print_port_status();
    print_contracts();
}

void print_boot_mode_status(const std::unique_ptr<TPS25751Mode> &modeReg)
{
    modeReg->debugPrint();

    if (auto bootFlagsReg = pd.readBootFlagsRegister(true))
        bootFlagsReg->debugPrint();
    else
        Serial.println(F("Failed to create/read Boot Flags register via factory"));
}

void print_patch_mode_status(const std::unique_ptr<TPS25751Mode> &modeReg)
{
    modeReg->debugPrint();

    if (auto bootFlagsReg = pd.readBootFlagsRegister(true))
        bootFlagsReg->debugPrint();
    else
        Serial.println(F("Failed to create/read Boot Flags register via factory"));
}

void print_status()
{
    Serial.println();
    Serial.println("-------------------- print_status --------------------");
    Serial.println();

    if (const auto modeReg = pd.readModeRegister(true))
    {
        switch (modeReg->getModeType())
        {
            case ModeType::Application:
                Serial.println(F("TPS25751 in Application mode"));
                print_app_mode_status(modeReg);
                break;
            case ModeType::Boot:
                Serial.println(F("TPS25751 in Boot mode"));
                print_boot_mode_status(modeReg);
                break;
            case ModeType::Patch:
                Serial.println(F("TPS25751 in Patch mode"));
                print_patch_mode_status(modeReg);
                break;
            default:
                Serial.println(F("TPS25751 in Unknown mode"));
                delay(10000);
                doReboot();
                break;
        }
    }
    else
        Serial.println(F("Failed to create/read Mode register via factory"));
}

void loop()
{
    Serial.println(); // Add spacing between iterations
    Serial.println("========================================================================================");
    Serial.print(F("[ Milliseconds elapsed: "));
    Serial.print(millis());
    Serial.println(F(" ] "));
    Serial.println("========================================================================================");
    Serial.println();

    print_status();

    delay(5000); // Give time to read output
}
