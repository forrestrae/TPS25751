#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

enum class PowerSource
{
    Reserved0 = 0,
    VIN_3V3 = 1,      // PD Controller is powered from VIN_3V3
    VBUS = 2,         // PD Controller is powered from VBUS
    Reserved3 = 3
};

enum class SwitchState
{
    Disabled = 0,
    DisabledDueToFault = 1,
    Enabled_CC1OrOutput = 2,
    Enabled_CC2OrSystemOutput = 3
};

class TPS25751PowerPathStatus : public TPS25751Register
{
public:
    explicit TPS25751PowerPathStatus() : TPS25751Register(TPS25751Registers::Registers::POWER_PATH_STATUS.size) {};
    explicit TPS25751PowerPathStatus(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::POWER_PATH_STATUS.size) {};
    TPS25751PowerPathStatus(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751PowerPathStatus(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    PowerSource powerSource() const;
    bool isPPCable1Overcurrent() const;
    bool isPP1Overcurrent() const;

    SwitchState ppCable1Switch() const;
    SwitchState pp1Switch() const;
    SwitchState pp3Switch() const;

    void debugPrint(Stream &s = Serial) const override;
    
    /**
     * @brief Validates power path status register data semantically
     * @return true if power path status flags are logically consistent
     */
    bool isSemanticallyValid() const override;
};

const char* toString(PowerSource src);
const char* toString(SwitchState state);
