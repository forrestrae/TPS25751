#include "TPS25751PowerPathStatus.h"

SwitchState TPS25751PowerPathStatus::ppCable1Switch() const
{
    return extractEnum<SwitchState>(0, 2, static_cast<uint8_t>(SwitchState::Enabled_CC2OrSystemOutput), SwitchState::Disabled);
}

SwitchState TPS25751PowerPathStatus::pp1Switch() const
{
    return extractEnum<SwitchState>(6, 3, static_cast<uint8_t>(SwitchState::Enabled_CC2OrSystemOutput), SwitchState::Disabled);
}

SwitchState TPS25751PowerPathStatus::pp3Switch() const
{
    return extractEnum<SwitchState>(12, 3, static_cast<uint8_t>(SwitchState::Enabled_CC2OrSystemOutput), SwitchState::Disabled);
}

bool TPS25751PowerPathStatus::isPP1Overcurrent() const
{
    return extractBits(28, 1);
}

bool TPS25751PowerPathStatus::isPPCable1Overcurrent() const
{
    return extractBits(34, 1);
}

PowerSource TPS25751PowerPathStatus::powerSource() const
{
    return extractEnum<PowerSource>(38, 2, static_cast<uint8_t>(PowerSource::Reserved3), PowerSource::Reserved0);
}

void TPS25751PowerPathStatus::debugPrint(Stream &s) const
{
    s.println(F("=== Power Path Status Register (0x26) ==="));
    s.print(F("  Power Source: "));
    s.println(toString(powerSource()));
    s.print(F("  PP_CABLE1 Overcurrent: "));
    s.println(isPPCable1Overcurrent() ? "Yes" : "No");
    s.print(F("  PP1 Overcurrent: "));
    s.println(isPP1Overcurrent() ? "Yes" : "No");
    s.print(F("  PP_CABLE1 Switch: "));
    s.println(toString(ppCable1Switch()));
    s.print(F("  PP1 Switch: "));
    s.println(toString(pp1Switch()));
    s.print(F("  PP3 Switch: "));
    s.println(toString(pp3Switch()));
    s.println();
}

const char *toString(PowerSource src)
{
    switch (src)
    {
        case PowerSource::VBUS:
            return "VBUS";
        case PowerSource::VIN_3V3:
            return "VIN_3V3";
        case PowerSource::Reserved0:
            return "Reserved (0)";
        case PowerSource::Reserved3:
            return "Reserved (3)";
        default:
            return "Invalid";
    }
}

const char *toString(SwitchState state)
{
    switch (state)
    {
        case SwitchState::Disabled:
            return "Disabled";
        case SwitchState::DisabledDueToFault:
            return "Disabled (Fault)";
        case SwitchState::Enabled_CC1OrOutput:
            return "Enabled (CC1/Output)";
        case SwitchState::Enabled_CC2OrSystemOutput:
            return "Enabled (CC2/System Output)";
        default:
            return "Invalid";
    }
}

bool TPS25751PowerPathStatus::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;
    
    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::POWER_PATH_STATUS.size;
    if (!isValidSize(expectedSize)) return false;
    
    // Validate enum ranges
    PowerSource pwrSource = powerSource();
    uint8_t pwrSourceRaw = static_cast<uint8_t>(pwrSource);
    if (pwrSourceRaw > 3) {
        return false;
    }
    
    // Validate switch states
    SwitchState cable1 = ppCable1Switch();
    SwitchState pp1 = pp1Switch();
    SwitchState pp3 = pp3Switch();
    
    uint8_t cable1Raw = static_cast<uint8_t>(cable1);
    uint8_t pp1Raw = static_cast<uint8_t>(pp1);
    uint8_t pp3Raw = static_cast<uint8_t>(pp3);
    
    if (cable1Raw > 3 || pp1Raw > 3 || pp3Raw > 3) {
        return false;
    }
    
    // Logical consistency checks
    
    // If overcurrent is detected, relevant switch should be disabled due to fault
    if (isPPCable1Overcurrent()) {
        if (cable1 != SwitchState::DisabledDueToFault && cable1 != SwitchState::Disabled) {
            return false;
        }
    }
    
    if (isPP1Overcurrent()) {
        if (pp1 != SwitchState::DisabledDueToFault && pp1 != SwitchState::Disabled) {
            return false;
        }
    }
    
    // Reserved power source values should be flagged
    if (pwrSource == PowerSource::Reserved0 || pwrSource == PowerSource::Reserved3) {
        // These could be warnings rather than hard failures in some contexts
    }
    
    return true;
}
