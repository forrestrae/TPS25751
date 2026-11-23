#pragma once

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

/**
 * @brief Switch state code shared by PP_CABLE1 (bits 1:0), PP1 (bits 8:6), and
 * PP3 (bits 14:12)
 *
 * The enumerator names follow PP_CABLE1. The same code means different things
 * per switch (SLVUCR8B Rev. B, p.27): code 2 is "CC1 enabled (system output)"
 * for PP_CABLE1 but "enabled (system output)" for PP1 and PP3, and code 3 is
 * "CC2 enabled (system output)" for PP_CABLE1 but "enabled (system input)" for
 * PP3. PP1 defines codes 0 to 2 only, and PP3 codes 0 to 3. Use the per-switch
 * text functions (TPS25751PowerPathStatus::pp3SwitchText() and the others) to
 * print a state; toString(SwitchState) gives the PP_CABLE1 wording.
 */
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

    /// @brief PP_CABLE1 switch state (bits 1:0). Every code 0 to 3 is defined.
    SwitchState ppCable1Switch() const;
    /**
     * @brief PP1 switch state (bits 8:6)
     *
     * Codes 3 to 7 are undefined in the TRM and decode as SwitchState::Disabled;
     * rawPp1Switch() returns the code as read, and isSemanticallyValid() rejects
     * an undefined code.
     */
    SwitchState pp1Switch() const;
    /**
     * @brief PP3 switch state (bits 14:12)
     *
     * Code 3 (SwitchState::Enabled_CC2OrSystemOutput) means "PP3 switch enabled
     * (system input)" for PP3. Codes 4 to 7 are undefined in the TRM and decode
     * as SwitchState::Disabled; rawPp3Switch() returns the code as read, and
     * isSemanticallyValid() rejects an undefined code.
     */
    SwitchState pp3Switch() const;

    /// @brief Raw PP_CABLE1 switch code (bits 1:0), 0 to 3.
    uint8_t rawPpCable1Switch() const;
    /// @brief Raw PP1 switch code (bits 8:6), 0 to 7; only 0 to 2 are defined.
    uint8_t rawPp1Switch() const;
    /// @brief Raw PP3 switch code (bits 14:12), 0 to 7; only 0 to 3 are defined.
    uint8_t rawPp3Switch() const;

    /// @brief TRM wording for a PP_CABLE1 switch code; "Undefined" past 3.
    static const char *ppCable1SwitchText(uint8_t raw);
    /// @brief TRM wording for a PP1 switch code; "Undefined" past 2.
    static const char *pp1SwitchText(uint8_t raw);
    /// @brief TRM wording for a PP3 switch code; "Undefined" past 3.
    static const char *pp3SwitchText(uint8_t raw);

    /// @brief Highest PP1 switch code the TRM defines (p.27).
    static constexpr uint8_t kPp1SwitchMaxDefined = 2;
    /// @brief Highest PP3 switch code the TRM defines (p.27).
    static constexpr uint8_t kPp3SwitchMaxDefined = 3;

    void debugPrint(Stream &s = Serial) const override;
    
    /**
     * @brief Validates power path status register data semantically
     *
     * Rejects a PP1 switch code above 2 or a PP3 switch code above 3 (undefined
     * in the TRM, p.27), and an overcurrent flag whose switch is enabled.
     *
     * @return true if power path status flags are logically consistent
     */
    bool isSemanticallyValid() const override;
};

const char* toString(PowerSource src);
const char* toString(SwitchState state);
