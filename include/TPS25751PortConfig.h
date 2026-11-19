#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

enum class TypeCStateMachine : uint8_t
{
    SinkOnly = 0,
    SourceOnly = 1,
    DRP = 2,
    Disabled = 3
};

/**
 * @brief TypeC Support Options (PORT_CONFIGURATION bits 9:8)
 *
 * Optional Type-C states supported as a DRP; the options are mutually
 * exclusive. Source: SLVUCR8B (Rev. B, 2026-08), page 29. Rev. B adds 2h,
 * Try.SNK. DRP_Src is a deprecated alias for TrySRC with the same value.
 */
enum class TypeCSupportOptions : uint8_t
{
    None = 0,       ///< No Type-C optional states are supported
    TrySRC = 1,     ///< Try.SRC state is supported as a DRP
    TrySNK = 2,     ///< Try.SNK state is supported as a DRP (new in Rev. B)
    Reserved = 3,   ///< Reserved

    /// @deprecated Pre-Rev. B name for 1h. Same value as TrySRC.
    DRP_Src [[deprecated("SLVUCR8B Rev. B p.29 names 1h Try.SRC; use TypeCSupportOptions::TrySRC")]] = 1
};

/**
 * @brief VBUS OVP Usage (PORT_CONFIGURATION bits 17:16)
 *
 * Source: SLVUCR8B (Rev. B, 2026-08), page 29. Rev. B lists 1h = 105% and
 * 2h = 111% (reset 2h), but 0h = "0.41 V/ms (typical)" and 3h = "3.39 V/ms
 * (typical)". Slew rates do not fit an OVP trip-point field and look like a TI
 * transcription error; Rev. A listed 0h = 100% and 3h = 114%, which this enum
 * keeps until TI clarifies.
 */
enum class VbusOVPUsage : uint8_t
{
    OVP100 = 0,
    OVP105 = 1,
    OVP111 = 2,
    OVP114 = 3
};

enum class OVP_PP5V : uint8_t
{
    V5_25 = 0,
    V5_5 = 1,
    V5_8 = 2,
    V6_1 = 3
};

enum class VbusUVPTripHV : uint8_t
{
    Margin5 = 0,
    Margin10 = 1,
    Margin15 = 2,
    Margin20 = 3,
    Margin25 = 4,
    Margin30 = 5,
    Margin40 = 6,
    Margin50 = 7
};

/**
 * @brief USB3 Rate (PORT_CONFIGURATION bits 14:13)
 *
 * USB3 signaling rate the port supports. Source: SLVUCR8B (Rev. B, 2026-08),
 * page 29; reset 0h. New in Rev. B.
 */
enum class USB3Rate : uint8_t
{
    NotSupported = 0,   ///< USB3 not supported
    Gen1 = 1,           ///< USB3 Gen1 signaling rate supported
    Gen2 = 2,           ///< USB3 Gen2 signaling rate supported
    Reserved = 3        ///< Reserved
};

/**
 * @brief APDO ILIM Over Shoot (PORT_CONFIGURATION bits 30:29)
 *
 * Current-limit overshoot applied when the port is a source and the
 * negotiated contract is a variable (APDO) type; it raises the current limit
 * of a supported BQ device. Source: SLVUCR8B (Rev. B, 2026-08), page 29;
 * reset 0h. All four values are defined.
 */
enum class ApdoIlimOvershoot : uint8_t
{
    Overshoot25mA = 0,  ///< 25 mA overshoot for APDO current
    Above50mA = 1,      ///< 50 mA above the negotiated variable PDO current
    Above75mA = 2,      ///< 75 mA above the negotiated variable PDO current
    Static2900mA = 3    ///< Static 2900 mA current limit
};

/**
 * @brief APDO VBUS UVP Threshold (PORT_CONFIGURATION bits 28:27)
 *
 * VBUS undervoltage threshold applied when the port is a source and the
 * negotiated contract is a variable (APDO) type. Source: SLVUCR8B (Rev. B,
 * 2026-08), page 29; reset 0h. The TRM wording is "N% below negotiated
 * variable PDO voltage".
 */
enum class ApdoVbusUvpThreshold : uint8_t
{
    Percent90 = 0,      ///< 90% below negotiated variable PDO voltage (TRM wording)
    Percent88 = 1,      ///< 88% below negotiated variable PDO voltage (TRM wording)
    Percent92 = 2,      ///< 92% below negotiated variable PDO voltage (TRM wording)
    Reserved = 3        ///< Reserved
};

class TPS25751PortConfig : public TPS25751Register
{
public:
    explicit TPS25751PortConfig() : TPS25751Register(TPS25751Registers::Registers::PORT_CONFIGURATION.size) {};
    explicit TPS25751PortConfig(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::PORT_CONFIGURATION.size) {};
    TPS25751PortConfig(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751PortConfig(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    TypeCStateMachine typeCStateMachine() const;

    TypeCSupportOptions typeCSupportOptions() const;

    bool disablePD() const;

    VbusOVPUsage vbusOVPUsage() const;

    OVP_PP5V ovpPP5V() const;

    VbusUVPTripHV vbusUVPTripHV() const;

    /// USB3 Rate (bits 14:13). New in Rev. B.
    USB3Rate usb3Rate() const;

    /// DebugAccessory Support (bit 12): set to enable DebugAccessory support. New in Rev. B.
    bool debugAccessorySupport() const;

    /**
     * @brief USB Communication Capable (bit 11)
     *
     * Set in systems that are USB communications capable; the controller
     * reports it in the USB communications capable field of PDO 1. New in Rev. B.
     */
    bool usbCommunicationCapable() const;

    /// APDO ILIM Over Shoot (bits 30:29). Value table added in Rev. B.
    ApdoIlimOvershoot apdoIlimOvershoot() const;

    /// APDO VBUS UVP Threshold (bits 28:27). Value table added in Rev. B.
    ApdoVbusUvpThreshold apdoVbusUvpThreshold() const;

    void debugPrint(Stream &s = Serial) const override ;

    /**
     * @brief Validates port configuration register data semantically
     * @return true if port configuration is logically consistent
     */
    bool isSemanticallyValid() const override;
};

// String conversion helpers for the Rev. B fields.
const char* toString(USB3Rate val);
const char* toString(ApdoIlimOvershoot val);
const char* toString(ApdoVbusUvpThreshold val);
