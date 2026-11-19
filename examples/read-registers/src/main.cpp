#include <Arduino.h>
#include <TPS25751.h>
#include <TPS25751Status.h>
#include <TPS25751Mode.h>
#include <TPS25751PowerPathStatus.h>
#include <TPS25751BootFlags.h>
#include <TPS25751PortConfig.h>
#include <TPS25751RegisterFactory.h>
#include "TPS25751InterruptEvent.h"
#include "TPS25751InterruptMask.h"
#include "TPS25751SleepControl.h"
#include "TPS25751CustomerUse.h"
#include "TPS25751ADCResults.h"
#include "TPS25751AutoNegotiateSink.h"
#include "TPS25751IOConfig.h"
#include "TPS25751LiquidDetectionConfig.h"
#include "TPS25751Version.h"
#include "TPS25751PD3Config.h"

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


    if (const auto txSourceCaps = pd.readTransmitSourceCapsRegister(true))
        txSourceCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read Transmit Source Capabilities register via factory"));


    if (const auto txSinkCaps = pd.readTransmitSinkCapsRegister(true))
        txSinkCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read Transmit Sink Capabilities register via factory"));


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

void print_adc_results()
{
    Serial.println();
    Serial.println("-------------------- print_adc_results --------------------");
    Serial.println();

    if (const auto adcResults = pd.readADCResultsRegister(true))
        adcResults->debugPrint();
    else
        Serial.println(F("Failed to create/read ADC Results register via factory"));
}

void print_sleep_and_customer_use()
{
    Serial.println();
    Serial.println("-------------------- print_sleep_and_customer_use --------------------");
    Serial.println();

    TPS25751SleepControl sleepControl;
    if (pd.readSleepControl(sleepControl))
    {
        sleepControl.debugPrint();

        // Raw register byte, for comparing against the TRM reset value (03h).
        Serial.print(F("SLEEP_CONTROL raw: 0x"));
        if (sleepControl.raw()[0] < 0x10)
            Serial.print('0');
        Serial.println(sleepControl.raw()[0], HEX);
    }
    else
        Serial.println(F("Failed to read Sleep Control register"));

    TPS25751CustomerUse customerUse;
    if (pd.readCustomerUse(customerUse))
        customerUse.debugPrint();
    else
        Serial.println(F("Failed to read Customer Use register"));
}

void print_interrupt_mask()
{
    Serial.println();
    Serial.println("-------------------- print_interrupt_mask --------------------");
    Serial.println();

    // INT_MASK1 (0x16) is readable; it gates which INT_EVENT1 sources may
    // assert IRQ. The companion INT_CLEAR1 (0x18) is not demonstrated here:
    // writing it is destructive (it clears events) and this sketch is
    // read-only / non-destructive.
    TPS25751InterruptMask interruptMask;
    if (pd.readInterruptMask(interruptMask))
        interruptMask.debugPrint();
    else
        Serial.println(F("Failed to read Interrupt Mask register"));
}

void print_autonegotiate_sink()
{
    Serial.println();
    Serial.println("-------------------- print_autonegotiate_sink --------------------");
    Serial.println();

    // AUTONEGOTIATE_SINK (0x37) is R/W, but writing sink-negotiation config to
    // a live board is a bricking risk -- this sketch is read-only, so only the
    // read path is demonstrated here.
    if (const auto autoNegSink = pd.readAutoNegotiateSinkRegister(true))
        autoNegSink->debugPrint();
    else
        Serial.println(F("Failed to create/read Autonegotiate Sink register via factory"));
}

void print_io_config()
{
    Serial.println();
    Serial.println("-------------------- print_io_config --------------------");
    Serial.println();

    // IO_CONFIG (0x5C) is read-only -- the TRM states GPIO configuration is
    // fixed at boot and cannot be modified at run-time.
    if (const auto ioConfig = pd.readIOConfigRegister(true))
        ioConfig->debugPrint();
    else
        Serial.println(F("Failed to create/read IO Config register via factory"));
}

void print_liquid_detection_config()
{
    Serial.println();
    Serial.println("-------------------- print_liquid_detection_config --------------------");
    Serial.println();

    // LIQUID_DETECTION_CONFIG (0x98) is R/W, but writing liquid-detection /
    // corrosion-mitigation config to a live board is a bricking risk -- this
    // sketch is read-only, so only the read path is demonstrated here.
    if (const auto liquidDetectionConfig = pd.readLiquidDetectionConfigRegister(true))
        liquidDetectionConfig->debugPrint();
    else
        Serial.println(F("Failed to create/read Liquid Detection Config register via factory"));
}

void print_version_and_pd3_config()
{
    Serial.println();
    Serial.println(F("-------------------- print_version_and_pd3_config --------------------"));
    Serial.println();

    // VERSION (0x0F) is read-only. PD3_CONFIGURATION (0x42) is R/W, but changing
    // which PD 3.0 messages a live board answers is outside this read-only
    // sketch, so only the read paths are demonstrated here.
    if (const auto version = pd.readVersionRegister(true))
        version->debugPrint();
    else
        Serial.println(F("Failed to create/read Version register via factory"));

    if (const auto pd3Config = pd.readPD3ConfigRegister(true))
        pd3Config->debugPrint();
    else
        Serial.println(F("Failed to create/read PD3 Configuration register via factory"));
}

void print_global_config_and_sop_identity()
{
    Serial.println();
    Serial.println(F("-------------------- print_global_config_and_sop_identity --------------------"));
    Serial.println();

    // GLOBAL_SYSTEM_CONFIGURATION (0x27) is R/W, but this sketch never writes it:
    // a write makes the port disconnect and reconnect, and a wrong PP1 or PP3
    // role can back-drive the power path. RECEIVED_SOP_IDENTITY (0x48) is read-only.
    if (const auto globalConfig = pd.readGlobalSystemConfigRegister(true))
        globalConfig->debugPrint();
    else
        Serial.println(F("Failed to create/read Global System Configuration register via factory"));

    if (const auto sopIdentity = pd.readReceivedSOPIdentityRegister(true))
        sopIdentity->debugPrint();
    else
        Serial.println(F("Failed to create/read Received SOP Identity register via factory"));

    // The identity register fills when the controller sends Discover Identity.
    // Automatic ID Request makes that automatic; Discover Mode Completed is a
    // later discovery step, not a "0x48 updated" signal.
    if (const auto portControl = pd.readPortControlRegister(true))
    {
        Serial.print(F("PORT_CONTROL Automatic ID Request: "));
        Serial.println(portControl->automaticIdRequest() ? F("Yes") : F("No"));
    }
    else
        Serial.println(F("Failed to create/read Port Control register via factory"));

    if (const auto interruptEvent = pd.readInterruptEventRegister(true))
    {
        Serial.print(F("INT_EVENT1 Discover Mode Completed: "));
        Serial.println(interruptEvent->discoverModeCompleted() ? F("Yes") : F("No"));
    }
    else
        Serial.println(F("Failed to create/read Interrupt Event register via factory"));
}

void print_source_info_and_pps_status()
{
    Serial.println();
    Serial.println(F("-------------------- print_source_info_and_pps_status --------------------"));
    Serial.println();

    // TX_SOURCE_INFO (0x78) and TRANSMITTED_PPS_STATUS (0x7A) are R/W, but this
    // sketch never writes them: the PD firmware updates 0x78 on its own, and a
    // wrong value in either register is sent to the port partner in reply to
    // Get_Source_Info or Get_PPS_Status.
    if (const auto sourceInfo = pd.readTransmitSourceInfoRegister(true))
        sourceInfo->debugPrint();
    else
        Serial.println(F("Failed to create/read TX Source Info register via factory"));

    if (const auto ppsStatus = pd.readTransmitPPSStatusRegister(true))
        ppsStatus->debugPrint();
    else
        Serial.println(F("Failed to create/read Transmitted PPS Status register via factory"));

    // The controller answers Get_Source_Info and Get_PPS_Status from these
    // registers only while the matching PD3_CONFIGURATION bit is set; with the
    // bit clear it replies Not_Supported.
    if (const auto pd3Config = pd.readPD3ConfigRegister(true))
    {
        Serial.print(F("PD3 Support Get Source Info: "));
        Serial.println(pd3Config->supportGetSourceInfo() ? F("Yes") : F("No"));
        Serial.print(F("PD3 Support PPS Status: "));
        Serial.println(pd3Config->supportPpsStatus() ? F("Yes") : F("No"));
    }
    else
        Serial.println(F("Failed to create/read PD3 Configuration register via factory"));
}

void print_caps_extended()
{
    Serial.println();
    Serial.println(F("-------------------- print_caps_extended --------------------"));
    Serial.println();

    // TX_SOURCE_CAPABILITIES_EXTENDED (0x77) and TRANSMIT_SINK_CAPABILITIES_EXTENDED
    // (0x7E) are R/W, but this sketch never writes them: whatever the host stores
    // is sent to the port partner in reply to Get_Source_Cap_Extended or
    // Get_Sink_Cap_Extended. The fields are raw; the USB PD specification defines
    // their encodings.
    if (const auto sourceCaps = pd.readTransmitSourceCapsExtendedRegister(true))
        sourceCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read TX Source Capabilities Extended register via factory"));

    if (const auto sinkCaps = pd.readTransmitSinkCapsExtendedRegister(true))
        sinkCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read Transmit Sink Capabilities Extended register via factory"));

    // The controller answers Get_Source_Cap_Extended and Get_Sink_Cap_Extended
    // from these registers only while the matching PD3_CONFIGURATION bit is set;
    // with the bit clear it replies Not_Supported.
    if (const auto pd3Config = pd.readPD3ConfigRegister(true))
    {
        Serial.print(F("PD3 Support Source Extended Message: "));
        Serial.println(pd3Config->supportSourceExtended() ? F("Yes") : F("No"));
        Serial.print(F("PD3 Support Sink Cap Extended: "));
        Serial.println(pd3Config->supportSinkCapExtended() ? F("Yes") : F("No"));
    }
    else
        Serial.println(F("Failed to create/read PD3 Configuration register via factory"));
}

void print_battery_data()
{
    Serial.println();
    Serial.println(F("-------------------- print_battery_data --------------------"));
    Serial.println();

    // TRANSMITTED_BATTERY_STATUS (0x7B) and TX_BATTERY_CAPABILITIES (0x7D) are R/W,
    // but this sketch never writes them: whatever the host stores is sent to the
    // port partner in reply to Get_Battery_Status or Get_Battery_Capabilities. The
    // fields are raw; the USB PD specification defines their encodings.
    if (const auto batteryStatus = pd.readTransmitBatteryStatusRegister(true))
        batteryStatus->debugPrint();
    else
        Serial.println(F("Failed to create/read Transmitted Battery Status register via factory"));

    if (const auto batteryCaps = pd.readTransmitBatteryCapabilitiesRegister(true))
        batteryCaps->debugPrint();
    else
        Serial.println(F("Failed to create/read TX Battery Capabilities register via factory"));

    // The controller answers Get_Battery_Status and Get_Battery_Capabilities from
    // these registers only while the matching PD3_CONFIGURATION bit is set; with
    // the bit clear it replies Not_Supported.
    if (const auto pd3Config = pd.readPD3ConfigRegister(true))
    {
        Serial.print(F("PD3 Support Battery Status Message: "));
        Serial.println(pd3Config->supportBatteryStatus() ? F("Yes") : F("No"));
        Serial.print(F("PD3 Support Battery Capabilities Message: "));
        Serial.println(pd3Config->supportBatteryCapabilities() ? F("Yes") : F("No"));
    }
    else
        Serial.println(F("Failed to create/read PD3 Configuration register via factory"));

    // The TRM wants the battery counts in TX_SOURCE_CAPABILITIES_EXTENDED (0x77) to
    // agree with the entries populated in 0x7B and 0x7D. Print them so the three
    // registers can be read against each other.
    if (const auto sourceCaps = pd.readTransmitSourceCapsExtendedRegister(true))
    {
        Serial.print(F("Number Fixed Batteries (0x77): "));
        Serial.println(sourceCaps->numberFixedBatteries());
        Serial.print(F("Number Hot Swappable Batteries (0x77): "));
        Serial.println(sourceCaps->numberHotSwappableBatteries());
    }
    else
        Serial.println(F("Failed to create/read TX Source Capabilities Extended register via factory"));
}

void print_app_mode_status(const std::unique_ptr<TPS25751Mode> &modeReg)
{
    modeReg->debugPrint();

    print_version_and_pd3_config();
    print_boot_flags();
    print_status_register();
    print_pd_status();
    print_power_status();
    print_adc_results();
    print_port_status();
    print_global_config_and_sop_identity();
    print_source_info_and_pps_status();
    print_caps_extended();
    print_battery_data();
    print_contracts();
    print_interrupt_mask();
    print_sleep_and_customer_use();
    print_autonegotiate_sink();
    print_io_config();
    print_liquid_detection_config();
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
