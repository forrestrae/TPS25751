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

uint8_t TPS25751PowerPathStatus::rawPpCable1Switch() const
{
    return extractBits(0, 2);
}

uint8_t TPS25751PowerPathStatus::rawPp1Switch() const
{
    return extractBits(6, 3);
}

uint8_t TPS25751PowerPathStatus::rawPp3Switch() const
{
    return extractBits(12, 3);
}

// Switch state wording per SLVUCR8B Rev. B, p.27. The codes are shared, the
// meanings are not: PP3 code 3 is a system input, PP_CABLE1 code 3 is CC2.
const char *TPS25751PowerPathStatus::ppCable1SwitchText(uint8_t raw)
{
    switch (raw)
    {
        case 0: return "Disabled";
        case 1: return "Currently disabled";
        case 2: return "CC1 enabled (system output)";
        case 3: return "CC2 enabled (system output)";
        default: return "Undefined";
    }
}

const char *TPS25751PowerPathStatus::pp1SwitchText(uint8_t raw)
{
    switch (raw)
    {
        case 0: return "Disabled";
        case 1: return "Disabled due to fault";
        case 2: return "Enabled (system output)";
        default: return "Undefined";
    }
}

const char *TPS25751PowerPathStatus::pp3SwitchText(uint8_t raw)
{
    switch (raw)
    {
        case 0: return "Disabled";
        case 1: return "Disabled due to fault";
        case 2: return "Enabled (system output)";
        case 3: return "Enabled (system input)";
        default: return "Undefined";
    }
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

namespace {

void printSwitch(Stream &s, const __FlashStringHelper *label, const char *text, uint8_t raw)
{
    s.print(label);
    s.print(text);
    s.print(F(" ("));
    s.print(raw);
    s.println(F(")"));
}

}  // namespace

void TPS25751PowerPathStatus::debugPrint(Stream &s) const
{
    s.println(F("=== Power Path Status Register (0x26) ==="));
    s.print(F("  Power Source: "));
    s.println(toString(powerSource()));
    s.print(F("  PP_CABLE1 Overcurrent: "));
    s.println(isPPCable1Overcurrent() ? "Yes" : "No");
    s.print(F("  PP1 Overcurrent: "));
    s.println(isPP1Overcurrent() ? "Yes" : "No");
    printSwitch(s, F("  PP_CABLE1 Switch: "), ppCable1SwitchText(rawPpCable1Switch()), rawPpCable1Switch());
    printSwitch(s, F("  PP1 Switch: "), pp1SwitchText(rawPp1Switch()), rawPp1Switch());
    printSwitch(s, F("  PP3 Switch: "), pp3SwitchText(rawPp3Switch()), rawPp3Switch());
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

    // The enum decoders map undefined codes to Disabled, so check the raw codes
    // against the TRM's defined range (p.27).
    if (rawPp1Switch() > kPp1SwitchMaxDefined || rawPp3Switch() > kPp3SwitchMaxDefined) {
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
