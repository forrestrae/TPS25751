#ifndef TPS25751STATUS_H
#define TPS25751STATUS_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

// Forward declare enums used for interpreting fields

/**
 * @brief STATUS Connection State (bits 3:1)
 *
 * Source: SLVUCR8B (Rev. B, 2026-08), page 25. Rev. B redefines 2h as
 * corrosion mitigation (TRM text: "Corrosion Mitigaion (Ra/Ra)"; Rev. A:
 * "Audio connection (Ra/Ra)") and 5h as "Debug connection (Rp/Rp)" (Rev. A:
 * Reserved). AUDIO_ACCESSORY and RESERVED are deprecated aliases with the
 * same values as CORROSION_MITIGATION and DEBUG_ACCESSORY_RP_RP.
 */
enum ConnectionState
{
    DISCONNECTED = 0,
    PORT_DISABLED = 1,
    CORROSION_MITIGATION = 2,       ///< 2h: Corrosion Mitigation (Ra/Ra); audio accessory in Rev. A
    DEBUG_ACCESSORY = 3,            ///< 3h: Debug connection (Rd/Rd)
    NO_CONNECTION_RA_DETECTED = 4,
    DEBUG_ACCESSORY_RP_RP = 5,      ///< 5h: Debug connection (Rp/Rp); Reserved in Rev. A
    CONNECTED_NO_RA_DETECTED = 6,
    CONNECTED_RA_DETECTED = 7,

    /// @deprecated Rev. A name for 2h. Same value as CORROSION_MITIGATION.
    AUDIO_ACCESSORY [[deprecated("SLVUCR8B Rev. B p.25 defines 2h as Corrosion Mitigation (Ra/Ra); use CORROSION_MITIGATION")]] = 2,
    /// @deprecated Rev. A name for 5h. Same value as DEBUG_ACCESSORY_RP_RP.
    RESERVED [[deprecated("SLVUCR8B Rev. B p.25 defines 5h as Debug connection (Rp/Rp); use DEBUG_ACCESSORY_RP_RP")]] = 5
};


enum VbusStatus
{
    VBUS_AT_VSAFE0V = 0,
    VBUS_AT_VSAFE5V = 1,
    VBUS_WITHIN_LIMITS = 2,
    VBUS_NOT_WITHIN_LIMITS = 3
};

enum UsbHostPresence
{
    USB_NO_HOST_PRESENT = 0,
    USB_HOST_SOURCE_NOT_DATA_CAPABLE = 1,
    USB_HOST_SOURCE_NOT_PD_CAPABLE = 2,
    USB_HOST_PRESENT = 3
};

enum ActingAsLegacy
{
    NOT_LEGACY = 0,
    LEGACY_SINK = 1,
    LEGACY_SOURCE = 2,
    LEGACY_SINK_DEAD_BATTERY = 3
};

enum PortRole
{
    PORT_SINK = 0,    // PD Controller is in the Sink role
    PORT_SOURCE = 1   // PD Controller is Source (CCx pull-up active)
};

enum DataRole
{
    UFP = 0,
    DFP = 1
};

enum PlugOrientation
{
    CC1 = 0,
    CC2 = 1
};

class TPS25751Status : public TPS25751Register
{
public:
    explicit TPS25751Status() : TPS25751Register(TPS25751Registers::Registers::STATUS.size) {};
    explicit TPS25751Status(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::STATUS.size) {};
    TPS25751Status(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751Status(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    bool plugPresent() const;
    ConnectionState connectionState() const;
    PlugOrientation plugOrientation() const;
    PortRole portRole() const;
    DataRole dataRole() const;
    VbusStatus vbusStatus() const;
    UsbHostPresence usbHostPresent() const;
    ActingAsLegacy actingAsLegacy() const;

    bool bistActive() const;
    bool socAckTimeout() const;
    void debugPrint(Stream &s = Serial) const override;
    
    /**
     * @brief Validates status register data semantically
     * @return true if status flags are logically consistent
     */
    bool isSemanticallyValid() const override;

};

// String conversion helpers
std::string toString(ConnectionState s);
std::string toString(VbusStatus s);
std::string toString(UsbHostPresence s);
std::string toString(ActingAsLegacy s);
std::string toString(PortRole s);
std::string toString(DataRole s);
std::string toString(PlugOrientation s);

#endif // TPS25751STATUS_H
