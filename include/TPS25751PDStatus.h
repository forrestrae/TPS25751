#ifndef TPS25751PDSTATUS_H
#define TPS25751PDSTATUS_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 PD Status Register (PD_STATUS, Address: 0x40)
 *
 * Status of PD and Type-C state-machine. This register contains details
 * regarding the status of PD messages and the Type-C state machine.
 *
 * Register size: 4 bytes (32 bits)
 * Access: Read-only
 */
class TPS25751PDStatus : public TPS25751Register
{
public:
    explicit TPS25751PDStatus() : TPS25751Register(TPS25751Registers::Registers::PD_STATUS.size) {};
    explicit TPS25751PDStatus(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::PD_STATUS.size) {};
    TPS25751PDStatus(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751PDStatus(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    /**
     * @brief Hard reset reason enumeration
     */
    enum class HardResetReason : uint8_t {
        NO_HARD_RESET = 0x00,
        RECEIVED_FROM_PORT_PARTNER = 0x01,
        REQUESTED_BY_HOST = 0x02,
        INVALID_DR_SWAP_DURING_ACTIVE_MODE = 0x03,
        DISCHARGE_FAILED = 0x04,
        NO_RESPONSE_TIMEOUT = 0x05,
        SEND_SOFT_RESET = 0x06,
        SINK_SELECT_CAPABILITY = 0x07,
        SINK_TRANSITION_SINK = 0x08,
        SINK_WAIT_FOR_CAPABILITIES = 0x09,
        SOFT_RESET = 0x0A,
        SOURCE_ON_TIMEOUT = 0x0B,
        SOURCE_CAPABILITY_RESPONSE = 0x0C,
        SOURCE_SEND_CAPABILITIES = 0x0D,
        SOURCING_FAULT = 0x0E,
        UNABLE_TO_SOURCE = 0x0F,
        FRS_FAILURE = 0x10,
        UNEXPECTED_MESSAGE = 0x11,
        VCONN_RECOVERY_TIMEOUT = 0x12
    };

    /**
     * @brief Soft reset reason enumeration
     */
    enum class SoftResetReason : uint8_t {
        NO_SOFT_RESET = 0x00,
        RECEIVED_FROM_PORT_PARTNER = 0x01,
        RESERVED_02 = 0x02,
        RESERVED_03 = 0x03,
        INVALID_SOURCE_CAPABILITIES = 0x04,
        MESSAGE_RETRIES_EXHAUSTED = 0x05,
        UNEXPECTED_ACCEPT = 0x06,
        UNEXPECTED_CONTROL_MESSAGE = 0x07,
        UNEXPECTED_GET_SINK_CAP = 0x08,
        UNEXPECTED_GET_SOURCE_CAP = 0x09,
        UNEXPECTED_GOTO_MIN = 0x0A,
        UNEXPECTED_PS_RDY = 0x0B,
        UNEXPECTED_PING = 0x0C,
        UNEXPECTED_REJECT = 0x0D,
        UNEXPECTED_REQUEST = 0x0E,
        UNEXPECTED_SINK_CAPABILITIES = 0x0F,
        UNEXPECTED_SOURCE_CAPABILITIES = 0x10,
        UNEXPECTED_SWAP = 0x11,
        UNEXPECTED_WAIT_CAPABILITIES = 0x12,
        UNKNOWN_CONTROL_MESSAGE = 0x13,
        UNKNOWN_DATA_MESSAGE = 0x14,
        INITIALIZE_SOP_PRIME_CONTROLLER = 0x15,
        INITIALIZE_SOP_DOUBLE_PRIME_CONTROLLER = 0x16,
        UNEXPECTED_EXTENDED_MESSAGE = 0x17,
        UNKNOWN_EXTENDED_MESSAGE = 0x18,
        UNEXPECTED_DATA_MESSAGE = 0x19,
        UNEXPECTED_NOT_SUPPORTED = 0x1A,
        UNEXPECTED_GET_STATUS = 0x1B
    };

    /**
     * @brief PD power role enumeration
     */
    enum class PDRole : uint8_t {
        SINK = 0,
        SOURCE = 1
    };

    /**
     * @brief Type-C port type enumeration
     */
    enum class PortType : uint8_t {
        SINK_SOURCE = 0,
        SINK = 1,
        SOURCE = 2,
        SOURCE_SINK = 3
    };

    /**
     * @brief CC pull-up value enumeration
     */
    enum class CCPullup : uint8_t {
        NOT_DETECTED = 0,           // Not in CC pull-down mode / no CC pull-up detected
        USB_DEFAULT = 1,            // USB Default current
        CURRENT_1_5A = 2,           // 1.5 A (SinkTxNG)
        CURRENT_3_0A = 3            // 3.0 A (SinkTxOK)
    };

    // Hard Reset Details (bits 21:16)
    HardResetReason getHardResetReason() const;

    // Soft Reset Details (bits 12:8)
    SoftResetReason getSoftResetReason() const;

    // Present PD Role (bit 6)
    PDRole getPDRole() const;

    // Port Type (bits 5:4)
    PortType getPortType() const;

    // CC Pullup (bits 3:2)
    CCPullup getCCPullup() const;

    /**
     * @brief Get human-readable hard reset reason string
     */
    const char* getHardResetReasonString() const;

    /**
     * @brief Get human-readable soft reset reason string
     */
    const char* getSoftResetReasonString() const;

    /**
     * @brief Get human-readable PD role string
     */
    const char* getPDRoleString() const;

    /**
     * @brief Get human-readable port type string
     */
    const char* getPortTypeString() const;

    /**
     * @brief Get human-readable CC pullup string
     */
    const char* getCCPullupString() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates PD status register data semantically
     * @return true if PD status data is logically consistent
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751PDSTATUS_H
