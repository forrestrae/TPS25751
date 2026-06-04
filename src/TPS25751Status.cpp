#include <Stream.h>
#include <string>

#include "TPS25751Status.h"




bool TPS25751Status::plugPresent() const
{
    return extractBits(0, 1);
}

ConnectionState TPS25751Status::connectionState() const
{
    return extractEnum<ConnectionState>(1, 3, CONNECTED_RA_DETECTED, DISCONNECTED);
}

PlugOrientation TPS25751Status::plugOrientation() const
{
    return extractEnum<PlugOrientation>(4, 1, CC2, CC1);
}

PortRole TPS25751Status::portRole() const
{
    return extractEnum<PortRole>(5, 1, PORT_SINK, PORT_SOURCE);
}

DataRole TPS25751Status::dataRole() const
{
    return extractEnum<DataRole>(6, 1, DFP, UFP);
}

VbusStatus TPS25751Status::vbusStatus() const
{
    return extractEnum<VbusStatus>(20, 2, VBUS_NOT_WITHIN_LIMITS, VBUS_AT_VSAFE0V);
}

UsbHostPresence TPS25751Status::usbHostPresent() const
{
    return extractEnum<UsbHostPresence>(22, 2, USB_HOST_PRESENT, USB_NO_HOST_PRESENT);
}

ActingAsLegacy TPS25751Status::actingAsLegacy() const
{
    return extractEnum<ActingAsLegacy>(24, 2, LEGACY_SINK_DEAD_BATTERY, NOT_LEGACY);
}

bool TPS25751Status::bistActive() const
{
    return extractBits(27, 1);
}

bool TPS25751Status::socAckTimeout() const
{
    return extractBits(30, 1);
}

void TPS25751Status::debugPrint(Stream &s) const
{
    s.println(F("=== Status Register (0x1A) ==="));
    s.print(F("  Plug Present: "));
    s.println(plugPresent() ? "Yes" : "No");
    s.print(F("  Connection State: "));
    s.println(toString(connectionState()).c_str());
    s.print(F("  Plug Orientation: "));
    s.println(toString(plugOrientation()).c_str());
    s.print(F("  Port Role: "));
    s.println(toString(portRole()).c_str());
    s.print(F("  Data Role: "));
    s.println(toString(dataRole()).c_str());
    s.print(F("  VBUS Status: "));
    s.println(toString(vbusStatus()).c_str());
    s.print(F("  USB Host Present: "));
    s.println(toString(usbHostPresent()).c_str());
    s.print(F("  Acting As Legacy: "));
    s.println(toString(actingAsLegacy()).c_str());
    s.print(F("  BIST Active: "));
    s.println(bistActive() ? "BIST in progress" : "No BIST in progress");
    s.print(F("  SoC ACK Timeout: "));
    s.println(socAckTimeout() ? "SoC has responded timely" : "SoC has not responded timely");
    s.println();
}

// String conversion helpers
std::string toString(const ConnectionState s)
{
    switch (s)
    {
        case DISCONNECTED:
            return "No connection";
        case PORT_DISABLED:
            return "Port is disable";
        case AUDIO_ACCESSORY:
            return "Audio connection (Ra/Ra)";
        case DEBUG_ACCESSORY:
            return "Debug connection (Rd/Rd)";
        case NO_CONNECTION_RA_DETECTED:
            return "No connection - Ra detected (Ra but no Rd)";
        case RESERVED:
            return "Reserved (may be used for Rp/Rp Debug connection)";
        case CONNECTED_NO_RA_DETECTED:
            return "Connection present - no Ra detected";
        case CONNECTED_RA_DETECTED:
            return "Connection present - Ra detected";
        default:
            return "Unknown";
    }
}

std::string toString(const VbusStatus s)
{
    switch (s)
    {
        case VBUS_AT_VSAFE0V:
            return "At vSafe0V (less than 0.8V)";
        case VBUS_AT_VSAFE5V:
            return "At vSafe5V (4.75V to 5.5V)";
        case VBUS_WITHIN_LIMITS:
            return "Within expected limits";
        case VBUS_NOT_WITHIN_LIMITS:
            return "Not within any of the other specified ranges";
        default:
            return "Unknown";
    }
}

std::string toString(const UsbHostPresence s)
{
    switch (s)
    {
        case USB_NO_HOST_PRESENT:
            return "No host present";
        case USB_HOST_SOURCE_NOT_DATA_CAPABLE:
            return "Attached source is not data capable";
        case USB_HOST_SOURCE_NOT_PD_CAPABLE:
            return "Attached source is not USB PD capable";
        case USB_HOST_PRESENT:
            return "Host Present";
        default:
            return "Reserved";
    }
}

std::string toString(const ActingAsLegacy s)
{
    switch (s)
    {
        case NOT_LEGACY:
            return "PD Controller is not in a legacy (non PD) mode";
        case LEGACY_SINK:
            return "PD Controller is acting like a legacy sink";
        case LEGACY_SOURCE:
            return "PD Controller is acting like a legacy source";
        case LEGACY_SINK_DEAD_BATTERY:
            return "Acting as legacy sink due to dead-battery";
        default:
            return "Uknown";
    }
}


std::string toString(const PortRole s)
{
    return (s == PORT_SOURCE) ? "PD Controller is Source (CCx pull-up active)" : "PD Controller is in the Sink role";
}

std::string toString(const DataRole s)
{
    return (s == UFP) ? "Upward-facing port (UFP)" : "Downward-facing port (DFP)";
}

std::string toString(const PlugOrientation s)
{
    return (s == CC1) ? "Upside-up orientation (plug CC on CC1)" : "Upside-down orientation (plug CC on CC2)";
}

bool TPS25751Status::isSemanticallyValid() const {
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::STATUS.size;
    if (!isValidSize(expectedSize)) return false;
    
    // Validate logical consistency of status flags
    ConnectionState connState = connectionState();
    VbusStatus vbus = vbusStatus();
    UsbHostPresence host = usbHostPresent();
    bool isPlugPresent = plugPresent();
    
    // If plug is present, connection state should not be DISCONNECTED
    if (isPlugPresent && connState == DISCONNECTED) {
        return false;
    }
    
    // If connection state is not DISCONNECTED, plug should be present
    if (!isPlugPresent && connState != DISCONNECTED) {
        return false;
    }
    
    // If USB host is present, connection should be established
    if (host == USB_HOST_SOURCE_NOT_DATA_CAPABLE && connState == DISCONNECTED) {
        return false;
    }
    
    // VBUS logic validation
    if (vbus == VBUS_AT_VSAFE5V || vbus == VBUS_WITHIN_LIMITS) {
        // If VBUS has voltage, some connection should exist
        if (connState == DISCONNECTED) {
            return false;
        }
    }
    
    // Validate enum ranges - check that extracted enum values are within valid ranges
    uint8_t connStateRaw = static_cast<uint8_t>(connState);
    uint8_t vbusRaw = static_cast<uint8_t>(vbus);
    uint8_t hostRaw = static_cast<uint8_t>(host);
    
    if (connStateRaw > 7 || vbusRaw > 3 || hostRaw > 3) {
        return false;
    }
    
    // Check for reserved enum values that should be validated
    ActingAsLegacy legacy = actingAsLegacy();
    uint8_t legacyRaw = static_cast<uint8_t>(legacy);
    if (legacyRaw > 3) {
        return false;
    }
    
    return true;
}