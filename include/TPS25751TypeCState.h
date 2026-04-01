#ifndef TPS25751TYPECSTATE_H
#define TPS25751TYPECSTATE_H

#include <string>
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

enum TypeCPortState
{
    TYPEC_DISABLED = 0x00,
    TYPEC_ERROR_RECOVERY = 0x05,
    TYPEC_UNATTACHED_ACCESSORY = 0x24,
    TYPEC_ATTACH_WAIT_ACCESSORY = 0x2B,
    TYPEC_TRY_SRC = 0x45,
    TYPEC_TRY_WAIT_SNK = 0x4E,
    TYPEC_TRY_SNK = 0x4F,
    TYPEC_TRY_WAIT_SRC = 0x50,
    TYPEC_ATTACHED_SRC = 0x60,
    TYPEC_ATTACHED_SNK = 0x61,
    TYPEC_AUDIO_ACCESSORY = 0x62,
    TYPEC_DEBUG_ACCESSORY = 0x63,
    TYPEC_ATTACH_WAIT_SRC = 0x64,
    TYPEC_ATTACH_WAIT_SNK = 0x65,
    TYPEC_UNATTACHED_SNK = 0x66,
    TYPEC_UNATTACHED_SRC = 0x67
};

enum CCPinState
{
    CC_NOT_CONNECTED = 0,
    CC_RA_DETECTED = 1,
    CC_RD_DETECTED = 2,
    CC_USB_DEFAULT_ADVERTISEMENT = 3,
    CC_1_5A_ADVERTISEMENT = 4,
    CC_3_0A_ADVERTISEMENT = 5
};

enum CCPinForPD
{
    CC_NO_PD_CONNECTION = 0,
    CC1_USED_FOR_PD = 1,
    CC2_USED_FOR_PD = 2
};

class TPS25751TypeCState : public TPS25751Register
{
public:
    explicit TPS25751TypeCState() : TPS25751Register(TPS25751Registers::Registers::TYPEC_STATE.size) {};
    explicit TPS25751TypeCState(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TYPEC_STATE.size) {};
    TPS25751TypeCState(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751TypeCState(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    TypeCPortState getPortState() const;
    CCPinState getCC2PinState() const;
    CCPinState getCC1PinState() const;
    CCPinForPD getCCPinForPD() const;

    void debugPrint(Stream &s = Serial) const override;

    bool isSemanticallyValid() const override;

};

std::string toString(TypeCPortState state);
std::string toString(CCPinState state);
std::string toString(CCPinForPD state);

#endif // TPS25751TYPECSTATE_H