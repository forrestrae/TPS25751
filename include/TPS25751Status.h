#ifndef TPS25751STATUS_H
#define TPS25751STATUS_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

// Forward declare enums used for interpreting fields
enum ConnectionState
{
    DISCONNECTED = 0,
    PORT_DISABLED = 1,
    AUDIO_ACCESSORY = 2,
    DEBUG_ACCESSORY = 3,
    NO_CONNECTION_RA_DETECTED = 4,
    RESERVED = 5,
    CONNECTED_NO_RA_DETECTED = 6,
    CONNECTED_RA_DETECTED = 7
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
