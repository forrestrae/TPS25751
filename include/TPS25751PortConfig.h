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

enum class TypeCSupportOptions : uint8_t
{
    None = 0,
    DRP_Src = 1,
    Reserved = 3
};

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

    void debugPrint(Stream &s = Serial) const override ;
    
    /**
     * @brief Validates port configuration register data semantically
     * @return true if port configuration is logically consistent
     */
    bool isSemanticallyValid() const override;
};
