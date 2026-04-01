#include <Stream.h>
#include <string>

#include "TPS25751TypeCState.h"

TypeCPortState TPS25751TypeCState::getPortState() const
{
    uint8_t rawValue = extractBits(24, 8);
    switch (rawValue) {
        case 0x00: return TYPEC_DISABLED;
        case 0x05: return TYPEC_ERROR_RECOVERY;
        case 0x24: return TYPEC_UNATTACHED_ACCESSORY;
        case 0x2B: return TYPEC_ATTACH_WAIT_ACCESSORY;
        case 0x45: return TYPEC_TRY_SRC;
        case 0x4E: return TYPEC_TRY_WAIT_SNK;
        case 0x4F: return TYPEC_TRY_SNK;
        case 0x50: return TYPEC_TRY_WAIT_SRC;
        case 0x60: return TYPEC_ATTACHED_SRC;
        case 0x61: return TYPEC_ATTACHED_SNK;
        case 0x62: return TYPEC_AUDIO_ACCESSORY;
        case 0x63: return TYPEC_DEBUG_ACCESSORY;
        case 0x64: return TYPEC_ATTACH_WAIT_SRC;
        case 0x65: return TYPEC_ATTACH_WAIT_SNK;
        case 0x66: return TYPEC_UNATTACHED_SNK;
        case 0x67: return TYPEC_UNATTACHED_SRC;
        default: return TYPEC_DISABLED;
    }
}

CCPinState TPS25751TypeCState::getCC2PinState() const
{
    return extractEnum<CCPinState>(16, 8, CC_3_0A_ADVERTISEMENT, CC_NOT_CONNECTED);
}

CCPinState TPS25751TypeCState::getCC1PinState() const
{
    return extractEnum<CCPinState>(8, 8, CC_3_0A_ADVERTISEMENT, CC_NOT_CONNECTED);
}

CCPinForPD TPS25751TypeCState::getCCPinForPD() const
{
    return extractEnum<CCPinForPD>(0, 8, CC2_USED_FOR_PD, CC_NO_PD_CONNECTION);
}

void TPS25751TypeCState::debugPrint(Stream &s) const
{
    s.println(F("=== Type C State Register (0x69) ==="));
    s.print(F("  Port State: "));
    s.println(toString(getPortState()).c_str());
    s.print(F("  CC1 Pin State: "));
    s.println(toString(getCC1PinState()).c_str());
    s.print(F("  CC2 Pin State: "));
    s.println(toString(getCC2PinState()).c_str());
    s.print(F("  CC Pin for PD: "));
    s.println(toString(getCCPinForPD()).c_str());
    s.println();
}

bool TPS25751TypeCState::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::TYPEC_STATE.size;
    if (!isValidSize(expectedSize)) return false;

    CCPinState cc1State = getCC1PinState();
    CCPinState cc2State = getCC2PinState();
    CCPinForPD pdPin = getCCPinForPD();

    if (static_cast<uint8_t>(cc1State) > 5 || static_cast<uint8_t>(cc2State) > 5) {
        return false;
    }

    if (static_cast<uint8_t>(pdPin) > 2) {
        return false;
    }

    if (pdPin == CC1_USED_FOR_PD && cc1State == CC_NOT_CONNECTED) {
        return false;
    }

    if (pdPin == CC2_USED_FOR_PD && cc2State == CC_NOT_CONNECTED) {
        return false;
    }

    return true;
}

std::string toString(TypeCPortState state)
{
    switch (state) {
        case TYPEC_DISABLED:
            return "Disabled";
        case TYPEC_ERROR_RECOVERY:
            return "ErrorRecovery";
        case TYPEC_UNATTACHED_ACCESSORY:
            return "Unattached.Accessory";
        case TYPEC_ATTACH_WAIT_ACCESSORY:
            return "AttachWait.Accessory";
        case TYPEC_TRY_SRC:
            return "Try.SRC";
        case TYPEC_TRY_WAIT_SNK:
            return "TryWait.SNK";
        case TYPEC_TRY_SNK:
            return "Try.SNK";
        case TYPEC_TRY_WAIT_SRC:
            return "TryWait.SRC";
        case TYPEC_ATTACHED_SRC:
            return "Attached.SRC";
        case TYPEC_ATTACHED_SNK:
            return "Attached.SNK";
        case TYPEC_AUDIO_ACCESSORY:
            return "AudioAccessory";
        case TYPEC_DEBUG_ACCESSORY:
            return "DebugAccessory";
        case TYPEC_ATTACH_WAIT_SRC:
            return "AttachWait.SRC";
        case TYPEC_ATTACH_WAIT_SNK:
            return "AttachWait.SNK";
        case TYPEC_UNATTACHED_SNK:
            return "Unattached.SNK";
        case TYPEC_UNATTACHED_SRC:
            return "Unattached.SRC";
        default:
            return "Unknown";
    }
}

std::string toString(CCPinState state)
{
    switch (state) {
        case CC_NOT_CONNECTED:
            return "Not connected";
        case CC_RA_DETECTED:
            return "Ra detected (Source only)";
        case CC_RD_DETECTED:
            return "Rd detected (Source only)";
        case CC_USB_DEFAULT_ADVERTISEMENT:
            return "USB Default Advertisement detected (Sink only)";
        case CC_1_5A_ADVERTISEMENT:
            return "1.5A Advertisement detected (Sink only)";
        case CC_3_0A_ADVERTISEMENT:
            return "3.0A Advertisement detected (Sink only)";
        default:
            return "Unknown";
    }
}

std::string toString(CCPinForPD state)
{
    switch (state) {
        case CC_NO_PD_CONNECTION:
            return "Not connected";
        case CC1_USED_FOR_PD:
            return "CC1 is used for USB PD communication";
        case CC2_USED_FOR_PD:
            return "CC2 is used for USB PD communication";
        default:
            return "Unknown";
    }
}