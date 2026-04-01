#include <Stream.h>
#include <string>

#include "TPS25751PDStatus.h"

TPS25751PDStatus::HardResetReason TPS25751PDStatus::getHardResetReason() const
{
    return extractEnum<HardResetReason>(16, 6, static_cast<uint8_t>(HardResetReason::VCONN_RECOVERY_TIMEOUT), HardResetReason::NO_HARD_RESET);
}

TPS25751PDStatus::SoftResetReason TPS25751PDStatus::getSoftResetReason() const
{
    return extractEnum<SoftResetReason>(8, 5, static_cast<uint8_t>(SoftResetReason::UNEXPECTED_GET_STATUS), SoftResetReason::NO_SOFT_RESET);
}

TPS25751PDStatus::PDRole TPS25751PDStatus::getPDRole() const
{
    return extractEnum<PDRole>(6, 1, static_cast<uint8_t>(PDRole::SOURCE), PDRole::SINK);
}

TPS25751PDStatus::PortType TPS25751PDStatus::getPortType() const
{
    return extractEnum<PortType>(4, 2, static_cast<uint8_t>(PortType::SOURCE_SINK), PortType::SINK_SOURCE);
}

TPS25751PDStatus::CCPullup TPS25751PDStatus::getCCPullup() const
{
    return extractEnum<CCPullup>(2, 2, static_cast<uint8_t>(CCPullup::CURRENT_3_0A), CCPullup::NOT_DETECTED);
}

const char* TPS25751PDStatus::getHardResetReasonString() const
{
    switch (getHardResetReason()) {
        case HardResetReason::NO_HARD_RESET:
            return "No Hard Reset";
        case HardResetReason::RECEIVED_FROM_PORT_PARTNER:
            return "Received from Port Partner";
        case HardResetReason::REQUESTED_BY_HOST:
            return "Requested by Host";
        case HardResetReason::INVALID_DR_SWAP_DURING_ACTIVE_MODE:
            return "Invalid DR_Swap during Active Mode";
        case HardResetReason::DISCHARGE_FAILED:
            return "Discharge Failed";
        case HardResetReason::NO_RESPONSE_TIMEOUT:
            return "No Response Timeout";
        case HardResetReason::SEND_SOFT_RESET:
            return "Send Soft Reset";
        case HardResetReason::SINK_SELECT_CAPABILITY:
            return "Sink Select Capability";
        case HardResetReason::SINK_TRANSITION_SINK:
            return "Sink Transition Sink";
        case HardResetReason::SINK_WAIT_FOR_CAPABILITIES:
            return "Sink Wait for Capabilities";
        case HardResetReason::SOFT_RESET:
            return "Soft Reset";
        case HardResetReason::SOURCE_ON_TIMEOUT:
            return "Source On Timeout";
        case HardResetReason::SOURCE_CAPABILITY_RESPONSE:
            return "Source Capability Response";
        case HardResetReason::SOURCE_SEND_CAPABILITIES:
            return "Source Send Capabilities";
        case HardResetReason::SOURCING_FAULT:
            return "Sourcing Fault";
        case HardResetReason::UNABLE_TO_SOURCE:
            return "Unable to Source";
        case HardResetReason::FRS_FAILURE:
            return "FRS Failure";
        case HardResetReason::UNEXPECTED_MESSAGE:
            return "Unexpected Message";
        case HardResetReason::VCONN_RECOVERY_TIMEOUT:
            return "VCONN Recovery Timeout";
        default:
            return "Unknown";
    }
}

const char* TPS25751PDStatus::getSoftResetReasonString() const
{
    switch (getSoftResetReason()) {
        case SoftResetReason::NO_SOFT_RESET:
            return "No Soft Reset";
        case SoftResetReason::RECEIVED_FROM_PORT_PARTNER:
            return "Received from Port Partner";
        case SoftResetReason::RESERVED_02:
            return "Reserved";
        case SoftResetReason::RESERVED_03:
            return "Reserved";
        case SoftResetReason::INVALID_SOURCE_CAPABILITIES:
            return "Invalid Source Capabilities";
        case SoftResetReason::MESSAGE_RETRIES_EXHAUSTED:
            return "Message Retries Exhausted";
        case SoftResetReason::UNEXPECTED_ACCEPT:
            return "Unexpected Accept";
        case SoftResetReason::UNEXPECTED_CONTROL_MESSAGE:
            return "Unexpected Control Message";
        case SoftResetReason::UNEXPECTED_GET_SINK_CAP:
            return "Unexpected GetSinkCap";
        case SoftResetReason::UNEXPECTED_GET_SOURCE_CAP:
            return "Unexpected GetSourceCap";
        case SoftResetReason::UNEXPECTED_GOTO_MIN:
            return "Unexpected GotoMin";
        case SoftResetReason::UNEXPECTED_PS_RDY:
            return "Unexpected PS_RDY";
        case SoftResetReason::UNEXPECTED_PING:
            return "Unexpected Ping";
        case SoftResetReason::UNEXPECTED_REJECT:
            return "Unexpected Reject";
        case SoftResetReason::UNEXPECTED_REQUEST:
            return "Unexpected Request";
        case SoftResetReason::UNEXPECTED_SINK_CAPABILITIES:
            return "Unexpected Sink Capabilities";
        case SoftResetReason::UNEXPECTED_SOURCE_CAPABILITIES:
            return "Unexpected Source Capabilities";
        case SoftResetReason::UNEXPECTED_SWAP:
            return "Unexpected Swap";
        case SoftResetReason::UNEXPECTED_WAIT_CAPABILITIES:
            return "Unexpected Wait Capabilities";
        case SoftResetReason::UNKNOWN_CONTROL_MESSAGE:
            return "Unknown Control Message";
        case SoftResetReason::UNKNOWN_DATA_MESSAGE:
            return "Unknown Data Message";
        case SoftResetReason::INITIALIZE_SOP_PRIME_CONTROLLER:
            return "Initialize SOP' Controller";
        case SoftResetReason::INITIALIZE_SOP_DOUBLE_PRIME_CONTROLLER:
            return "Initialize SOP'' Controller";
        case SoftResetReason::UNEXPECTED_EXTENDED_MESSAGE:
            return "Unexpected Extended Message";
        case SoftResetReason::UNKNOWN_EXTENDED_MESSAGE:
            return "Unknown Extended Message";
        case SoftResetReason::UNEXPECTED_DATA_MESSAGE:
            return "Unexpected Data Message";
        case SoftResetReason::UNEXPECTED_NOT_SUPPORTED:
            return "Unexpected Not Supported";
        case SoftResetReason::UNEXPECTED_GET_STATUS:
            return "Unexpected Get_Status";
        default:
            return "Unknown";
    }
}

const char* TPS25751PDStatus::getPDRoleString() const
{
    switch (getPDRole()) {
        case PDRole::SINK:
            return "Sink";
        case PDRole::SOURCE:
            return "Source";
        default:
            return "Unknown";
    }
}

const char* TPS25751PDStatus::getPortTypeString() const
{
    switch (getPortType()) {
        case PortType::SINK_SOURCE:
            return "Sink/Source";
        case PortType::SINK:
            return "Sink";
        case PortType::SOURCE:
            return "Source";
        case PortType::SOURCE_SINK:
            return "Source/Sink";
        default:
            return "Unknown";
    }
}

const char* TPS25751PDStatus::getCCPullupString() const
{
    switch (getCCPullup()) {
        case CCPullup::NOT_DETECTED:
            return "Not Detected";
        case CCPullup::USB_DEFAULT:
            return "USB Default";
        case CCPullup::CURRENT_1_5A:
            return "1.5 A (SinkTxNG)";
        case CCPullup::CURRENT_3_0A:
            return "3.0 A (SinkTxOK)";
        default:
            return "Unknown";
    }
}

void TPS25751PDStatus::debugPrint(Stream &s) const
{
    s.println(F("=== PD Status Register (0x40) ==="));

    // PD Role
    s.print(F("  PD Role: "));
    s.println(getPDRoleString());

    // Port Type
    s.print(F("  Port Type: "));
    s.println(getPortTypeString());

    // CC Pullup
    s.print(F("  CC Pullup: "));
    s.println(getCCPullupString());

    // Hard Reset Reason
    auto hardResetReason = getHardResetReason();
    if (hardResetReason != HardResetReason::NO_HARD_RESET) {
        s.print(F("  Hard Reset Reason: "));
        s.println(getHardResetReasonString());
    }

    // Soft Reset Reason
    auto softResetReason = getSoftResetReason();
    if (softResetReason != SoftResetReason::NO_SOFT_RESET) {
        s.print(F("  Soft Reset Reason: "));
        s.println(getSoftResetReasonString());
    }

    s.println();
}

bool TPS25751PDStatus::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::PD_STATUS.size;
    if (!isValidSize(expectedSize)) return false;

    // Validate hard reset reason is in valid range
    uint8_t hardResetValue = extractBits(16, 6);
    if (hardResetValue > 0x12) {
        return false;
    }

    // Validate soft reset reason is in valid range
    uint8_t softResetValue = extractBits(8, 5);
    if (softResetValue > 0x1B) {
        return false;
    }

    // Port type should be in valid range (0-3)
    uint8_t portTypeValue = extractBits(4, 2);
    if (portTypeValue > 3) {
        return false;
    }

    // CC pullup should be in valid range (0-3)
    uint8_t ccPullupValue = extractBits(2, 2);
    if (ccPullupValue > 3) {
        return false;
    }

    return true;
}
