#include "TPS25751PortConfig.h"
#include "util/TPS25751BitUtils.h"


const char* toString(TypeCStateMachine state) {
    switch (state) {
        case TypeCStateMachine::SinkOnly: return "Sink Only";
        case TypeCStateMachine::SourceOnly: return "Source Only";
        case TypeCStateMachine::DRP: return "Dual Role Power (DRP)";
        case TypeCStateMachine::Disabled: return "Disabled";
        default: return "Unknown";
    }
}

const char* toString(TypeCSupportOptions opt) {
    switch (opt) {
        case TypeCSupportOptions::None: return "None";
        case TypeCSupportOptions::DRP_Src: return "DRP with Source";
        case TypeCSupportOptions::Reserved: return "Reserved";
        default: return "Unknown";
    }
}

const char* toString(VbusOVPUsage val) {
    switch (val) {
        case VbusOVPUsage::OVP100: return "100%";
        case VbusOVPUsage::OVP105: return "105%";
        case VbusOVPUsage::OVP111: return "111%";
        case VbusOVPUsage::OVP114: return "114%";
        default: return "Unknown";
    }
}

const char* toString(OVP_PP5V val) {
    switch (val) {
        case OVP_PP5V::V5_25: return "5.25V";
        case OVP_PP5V::V5_5:  return "5.5V";
        case OVP_PP5V::V5_8:  return "5.8V";
        case OVP_PP5V::V6_1:  return "6.1V";
        default: return "Unknown";
    }
}

const char* toString(VbusUVPTripHV val) {
    switch (val) {
        case VbusUVPTripHV::Margin5: return "5%";
        case VbusUVPTripHV::Margin10: return "10%";
        case VbusUVPTripHV::Margin15: return "15%";
        case VbusUVPTripHV::Margin20: return "20%";
        case VbusUVPTripHV::Margin25: return "25%";
        case VbusUVPTripHV::Margin30: return "30%";
        case VbusUVPTripHV::Margin40: return "40%";
        case VbusUVPTripHV::Margin50: return "50%";
        default: return "Unknown";
    }
}


TypeCStateMachine TPS25751PortConfig::typeCStateMachine() const
{
    return extractEnum<TypeCStateMachine>(0, 2, static_cast<uint8_t>(TypeCStateMachine::Disabled), TypeCStateMachine::SinkOnly);
}

TypeCSupportOptions TPS25751PortConfig::typeCSupportOptions() const
{
    return extractEnum<TypeCSupportOptions>(8, 2, static_cast<uint8_t>(TypeCSupportOptions::Reserved), TypeCSupportOptions::None);
}

bool TPS25751PortConfig::disablePD() const
{
    return extractBits(10, 1);
}

VbusOVPUsage TPS25751PortConfig::vbusOVPUsage() const
{
    return extractEnum<VbusOVPUsage>(16, 2, static_cast<uint8_t>(VbusOVPUsage::OVP114), VbusOVPUsage::OVP100);
}

OVP_PP5V TPS25751PortConfig::ovpPP5V() const
{
    return extractEnum<OVP_PP5V>(20, 2, static_cast<uint8_t>(OVP_PP5V::V6_1), OVP_PP5V::V5_25);
}

VbusUVPTripHV TPS25751PortConfig::vbusUVPTripHV() const
{
    return extractEnum<VbusUVPTripHV>(24, 3, static_cast<uint8_t>(VbusUVPTripHV::Margin50), VbusUVPTripHV::Margin5);
}

void TPS25751PortConfig::debugPrint(Stream &s) const {
    s.println(F("=== Port Configuration Register (0x28) ==="));
    s.print(F("  Type-C State Machine: ")); s.println(toString(typeCStateMachine()));
    s.print(F("  Type-C Support Options: ")); s.println(toString(typeCSupportOptions()));
    s.print(F("  Disable PD: ")); s.println(disablePD());
    s.print(F("  VBUS OVP Usage: ")); s.println(toString(vbusOVPUsage()));
    s.print(F("  OVP for PP5V: ")); s.println(toString(ovpPP5V()));
    s.print(F("  VBUS Sink UVP Trip HV: ")); s.println(toString(vbusUVPTripHV()));
    s.println();
}

bool TPS25751PortConfig::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;
    
    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::PORT_CONFIGURATION.size;
    if (!isValidSize(expectedSize)) return false;
    
    // Validate enum ranges
    TypeCStateMachine stateM = typeCStateMachine();
    TypeCSupportOptions support = typeCSupportOptions();
    VbusOVPUsage ovpUsage = vbusOVPUsage();
    OVP_PP5V ovp5v = ovpPP5V();
    VbusUVPTripHV uvpTrip = vbusUVPTripHV();
    
    uint8_t stateMRaw = static_cast<uint8_t>(stateM);
    uint8_t supportRaw = static_cast<uint8_t>(support);
    uint8_t ovpUsageRaw = static_cast<uint8_t>(ovpUsage);
    uint8_t ovp5vRaw = static_cast<uint8_t>(ovp5v);
    uint8_t uvpTripRaw = static_cast<uint8_t>(uvpTrip);
    
    // Check enum values are within valid ranges
    if (stateMRaw > 3 || supportRaw > 3 || ovpUsageRaw > 3 || ovp5vRaw > 3 || uvpTripRaw > 7) {
        return false;
    }
    
    // Logical consistency checks
    
    // If PD is disabled, certain support options may be irrelevant
    if (disablePD()) {
        // PD disabled - state machine should still be valid but some features won't work
    }
    
    // If state machine is disabled, should support options be "None"?
    if (stateM == TypeCStateMachine::Disabled && support != TypeCSupportOptions::None) {
        // This could be a warning rather than error - configuration may be for future use
    }
    
    // DRP support should be consistent with state machine
    if (support == TypeCSupportOptions::DRP_Src && stateM == TypeCStateMachine::SinkOnly) {
        return false;  // Inconsistent configuration
    }
    
    // Check for reserved enum values
    if (support == TypeCSupportOptions::Reserved) {
        return false;  // Reserved value used
    }
    
    return true;
}

