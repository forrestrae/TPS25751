#pragma once

#include <memory>
#include "TPS25751DownstreamDevice.h"
#include "BQ25798/BQ25798Registers.h"
#include "BQ25798/BQ25798PartInfo.h"
#include "BQ25798/BQ25798ChargerStatus0.h"
#include "BQ25798/BQ25798ChargerStatus1.h"
#include "BQ25798/BQ25798ChargerStatus2.h"
#include "BQ25798/BQ25798ChargerStatus3.h"
#include "BQ25798/BQ25798ChargerStatus4.h"
#include "BQ25798/BQ25798FaultStatus0.h"
#include "BQ25798/BQ25798FaultStatus1.h"
#include "BQ25798/BQ25798ChargerFlag0.h"
#include "BQ25798/BQ25798ChargerFlag1.h"
#include "BQ25798/BQ25798ChargerFlag2.h"
#include "BQ25798/BQ25798ChargerFlag3.h"
#include "BQ25798/BQ25798FaultFlag0.h"
#include "BQ25798/BQ25798FaultFlag1.h"
#include "BQ25798/BQ25798AdcControl.h"
#include "BQ25798/BQ25798AdcFunctionDisable0.h"
#include "BQ25798/BQ25798AdcFunctionDisable1.h"
#include "BQ25798/BQ25798IbusAdc.h"
#include "BQ25798/BQ25798IbatAdc.h"
#include "BQ25798/BQ25798VbusAdc.h"
#include "BQ25798/BQ25798Vac1Adc.h"
#include "BQ25798/BQ25798Vac2Adc.h"
#include "BQ25798/BQ25798VbatAdc.h"
#include "BQ25798/BQ25798VsysAdc.h"
#include "BQ25798/BQ25798TsAdc.h"
#include "BQ25798/BQ25798TdieAdc.h"
#include "BQ25798/BQ25798MinimalSystemVoltage.h"
#include "BQ25798/BQ25798ChargeVoltageLimit.h"
#include "BQ25798/BQ25798ChargeCurrentLimit.h"
#include "BQ25798/BQ25798InputVoltageLimit.h"
#include "BQ25798/BQ25798InputCurrentLimit.h"
#include "BQ25798/BQ25798PrechargeControl.h"
#include "BQ25798/BQ25798TerminationControl.h"
#include "BQ25798/BQ25798NtcControl0.h"
#include "BQ25798/BQ25798NtcControl1.h"
#include "BQ25798/BQ25798IcoCurrentLimit.h"
#include "BQ25798/BQ25798ChargerControl0.h"
#include "BQ25798/BQ25798ChargerControl1.h"
#include "BQ25798/BQ25798ChargerControl2.h"
#include "BQ25798/BQ25798ChargerControl3.h"
#include "BQ25798/BQ25798ChargerControl4.h"
#include "BQ25798/BQ25798ChargerControl5.h"
#include "BQ25798/BQ25798RechargeControl.h"
#include "BQ25798/BQ25798VotgRegulation.h"
#include "BQ25798/BQ25798IotgRegulation.h"
#include "BQ25798/BQ25798TimerControl.h"
#include "BQ25798/BQ25798MpptControl.h"
#include "BQ25798/BQ25798TemperatureControl.h"
#include "BQ25798/BQ25798ChargerMask0.h"
#include "BQ25798/BQ25798ChargerMask1.h"
#include "BQ25798/BQ25798ChargerMask2.h"
#include "BQ25798/BQ25798ChargerMask3.h"
#include "BQ25798/BQ25798FaultMask0.h"
#include "BQ25798/BQ25798FaultMask1.h"
#include "BQ25798/BQ25798DplusAdc.h"
#include "BQ25798/BQ25798DminusAdc.h"
#include "BQ25798/BQ25798DpdmDriver.h"

namespace BQ25798 {

/**
 * @brief BQ25798 charger driver — typed register access over the TPS25751 I2Cc bus
 *
 * This class is the BQ25798 analog of TPS25751: it owns no I2C bus directly.
 * All transactions are relayed through the TPS25751 host's executeCommand()
 * via the inherited TPS25751DownstreamDevice::readRegister() /
 * writeRegister() interface (I2Cr / I2Cw 4CC tasks).
 *
 * Design:
 *   - Inherits TPS25751DownstreamDevice (composition-by-inheritance, exactly
 *     like TPS25751 register classes inherit TPS25751Register).
 *   - Exposes typed accessor methods that return std::unique_ptr<RegisterClass>.
 *   - Does NOT subclass TPS25751 — the host is composed, not extended.
 *
 * Adding registers (Stage 3):
 *   1. Include the group header (e.g. BQ25798ChargerStatus.h).
 *   2. Add a typed accessor (e.g. readChargerStatus0(bool validate=true) const).
 *   3. Mark the TODO comment below at the accessor boundary.
 *
 * @note The default 7-bit address kDefaultI2CAddress = 0x6B must NOT have its
 *       R/W bit included — TPS25751DownstreamDevice masks bit 7 internally.
 */
class Device : public TPS25751DownstreamDevice
{
public:
    /**
     * @brief Construct a BQ25798 driver proxied through @p host
     * @param host TPS25751 instance whose executeCommand() relays I2Cr/I2Cw tasks
     * @param addr 7-bit I2C address of the BQ25798 (default: kDefaultI2CAddress)
     */
    explicit Device(const TPS25751& host,
                    uint8_t addr = BQ25798::kDefaultI2CAddress)
        : TPS25751DownstreamDevice(host, addr)
    {}

    // -----------------------------------------------------------------------
    // Typed register accessors
    // Each method reads the register via I2Cr, constructs the typed object,
    // and optionally verifies isSemanticallyValid() before returning.
    // Returns nullptr on I2C failure or (if validate=true) semantic failure.
    // -----------------------------------------------------------------------

    /**
     * @brief Read and decode the Part Information register (REG48h)
     * @param validate If true, returns nullptr when isSemanticallyValid() fails.
     * @return Decoded PartInfo, or nullptr on I2C failure or (if validate) semantic failure.
     */
    std::unique_ptr<PartInfo> readPartInfo(bool validate = true) const;

    // --- Status ---
    std::unique_ptr<ChargerStatus0> readChargerStatus0(bool validate = true) const;
    std::unique_ptr<ChargerStatus1> readChargerStatus1(bool validate = true) const;
    std::unique_ptr<ChargerStatus2> readChargerStatus2(bool validate = true) const;
    std::unique_ptr<ChargerStatus3> readChargerStatus3(bool validate = true) const;
    std::unique_ptr<ChargerStatus4> readChargerStatus4(bool validate = true) const;
    std::unique_ptr<FaultStatus0>   readFaultStatus0(bool validate = true) const;
    std::unique_ptr<FaultStatus1>   readFaultStatus1(bool validate = true) const;

    // --- Flags ---
    std::unique_ptr<ChargerFlag0> readChargerFlag0(bool validate = true) const;
    std::unique_ptr<ChargerFlag1> readChargerFlag1(bool validate = true) const;
    std::unique_ptr<ChargerFlag2> readChargerFlag2(bool validate = true) const;
    std::unique_ptr<ChargerFlag3> readChargerFlag3(bool validate = true) const;
    std::unique_ptr<FaultFlag0>   readFaultFlag0(bool validate = true) const;
    std::unique_ptr<FaultFlag1>   readFaultFlag1(bool validate = true) const;

    // --- ADC ---
    std::unique_ptr<AdcControl>          readAdcControl(bool validate = true) const;
    std::unique_ptr<AdcFunctionDisable0> readAdcFunctionDisable0(bool validate = true) const;
    std::unique_ptr<AdcFunctionDisable1> readAdcFunctionDisable1(bool validate = true) const;
    std::unique_ptr<IbusAdc>             readIbusAdc(bool validate = true) const;
    std::unique_ptr<IbatAdc>             readIbatAdc(bool validate = true) const;
    std::unique_ptr<VbusAdc>             readVbusAdc(bool validate = true) const;
    std::unique_ptr<Vac1Adc>             readVac1Adc(bool validate = true) const;
    std::unique_ptr<Vac2Adc>             readVac2Adc(bool validate = true) const;
    std::unique_ptr<VbatAdc>             readVbatAdc(bool validate = true) const;
    std::unique_ptr<VsysAdc>             readVsysAdc(bool validate = true) const;
    std::unique_ptr<TsAdc>               readTsAdc(bool validate = true) const;
    std::unique_ptr<TdieAdc>             readTdieAdc(bool validate = true) const;

    // --- Charge limits / thresholds ---
    std::unique_ptr<MinimalSystemVoltage> readMinimalSystemVoltage(bool validate = true) const;
    std::unique_ptr<ChargeVoltageLimit>   readChargeVoltageLimit(bool validate = true) const;
    std::unique_ptr<ChargeCurrentLimit>   readChargeCurrentLimit(bool validate = true) const;
    std::unique_ptr<InputVoltageLimit>    readInputVoltageLimit(bool validate = true) const;
    std::unique_ptr<InputCurrentLimit>    readInputCurrentLimit(bool validate = true) const;
    std::unique_ptr<PrechargeControl>     readPrechargeControl(bool validate = true) const;
    std::unique_ptr<TerminationControl>   readTerminationControl(bool validate = true) const;
    std::unique_ptr<NtcControl0>          readNtcControl0(bool validate = true) const;
    std::unique_ptr<NtcControl1>          readNtcControl1(bool validate = true) const;
    std::unique_ptr<IcoCurrentLimit>      readIcoCurrentLimit(bool validate = true) const;

    // --- Charger control ---
    std::unique_ptr<ChargerControl0> readChargerControl0(bool validate = true) const;
    std::unique_ptr<ChargerControl1> readChargerControl1(bool validate = true) const;
    std::unique_ptr<ChargerControl2> readChargerControl2(bool validate = true) const;
    std::unique_ptr<ChargerControl3> readChargerControl3(bool validate = true) const;
    std::unique_ptr<ChargerControl4> readChargerControl4(bool validate = true) const;
    std::unique_ptr<ChargerControl5> readChargerControl5(bool validate = true) const;

    // --- Recharge / OTG / Timer / MPPT / Temperature ---
    std::unique_ptr<RechargeControl>    readRechargeControl(bool validate = true) const;
    std::unique_ptr<VotgRegulation>     readVotgRegulation(bool validate = true) const;
    std::unique_ptr<IotgRegulation>     readIotgRegulation(bool validate = true) const;
    std::unique_ptr<TimerControl>       readTimerControl(bool validate = true) const;
    std::unique_ptr<MpptControl>        readMpptControl(bool validate = true) const;
    std::unique_ptr<TemperatureControl> readTemperatureControl(bool validate = true) const;

    // --- Charger / Fault masks ---
    std::unique_ptr<ChargerMask0> readChargerMask0(bool validate = true) const;
    std::unique_ptr<ChargerMask1> readChargerMask1(bool validate = true) const;
    std::unique_ptr<ChargerMask2> readChargerMask2(bool validate = true) const;
    std::unique_ptr<ChargerMask3> readChargerMask3(bool validate = true) const;
    std::unique_ptr<FaultMask0>   readFaultMask0(bool validate = true) const;
    std::unique_ptr<FaultMask1>   readFaultMask1(bool validate = true) const;

    // --- D+/D- ADC and DPDM driver ---
    std::unique_ptr<DplusAdc>   readDplusAdc(bool validate = true) const;
    std::unique_ptr<DminusAdc>  readDminusAdc(bool validate = true) const;
    std::unique_ptr<DpdmDriver> readDpdmDriver(bool validate = true) const;
};

}  // namespace BQ25798
