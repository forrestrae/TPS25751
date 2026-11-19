#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751ReceivedSOPIdentity.h
 * @brief RECEIVED_SOP_IDENTITY register (0x48) class implementation
 *
 * RECEIVED_SOP_IDENTITY_DATA_OBJECT holds the latest Discover Identity response
 * the controller received over USB PD using SOP. It carries a response type, a
 * count of valid VDOs (at most 6), and the VDOs themselves as raw 32-bit words.
 *
 * The VDO internals (ID Header, Cert Stat, Product, and the context-specific
 * fourth to sixth) are defined by the USB PD specification and are not decoded
 * here. Typed decode is planned as Batch L of the Rev B audit plan.
 *
 * The register is filled when the controller sends Discover Identity.
 * PORT_CONTROL Automatic ID Request (0x29 bit 16, see
 * TPS25751PortControl::automaticIdRequest()) makes the controller send it
 * automatically. INT_EVENT1 bit 51 (discoverModeCompleted()) marks a later
 * discovery step and is not a signal that this register was updated. Response
 * Type is the authoritative status.
 *
 * Register Details:
 * - Address: 0x48
 * - Size: 25 bytes (200 bits)
 * - Access: Read-only (no setters, no write path, no kAddress)
 * - Reset: all zero
 * - Byte 0: Number Valid VDOs (bits 2:0), reserved (5:3), Response Type (7:6)
 * - Bytes 1..24: VDO 1 to VDO 6, little-endian, 4 bytes each
 *
 * @code
 * auto sop = tps.readReceivedSOPIdentityRegister();
 * if (sop && sop->responseType() == TPS25751ReceivedSOPIdentity::ResponseType::Ack) {
 *     uint32_t idHeader = sop->idHeaderVdo();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.47
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751ReceivedSOPIdentity : public TPS25751Register
{
public:
    /// Type of response received (bits 7:6)
    enum class ResponseType : uint8_t {
        NotSentOrPending = 0,  ///< 0h - SOP Discover Identity REQ not sent or pending
        Ack = 1,               ///< 1h - Responder ACK received
        NakOrTimeout = 2,      ///< 2h - Responder NAK received or response timeout
        Busy = 3               ///< 3h - Responder BUSY received
    };

    /// Maximum number of VDOs the register holds
    static constexpr uint8_t kMaxVdos = 6;

    explicit TPS25751ReceivedSOPIdentity() : TPS25751Register(TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY.size) {}
    explicit TPS25751ReceivedSOPIdentity(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY.size) {}
    TPS25751ReceivedSOPIdentity(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ReceivedSOPIdentity(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Number Valid VDOs (bits 2:0). The TRM maximum is 6.
    uint8_t numberValidVdos() const;

    /// @brief Response Type (bits 7:6)
    ResponseType responseType() const;

    /**
     * @brief Raw 32-bit VDO by zero-based index
     *
     * Index 0 is the TRM's "RX ID SOP VDO 1" (bits 39:8), index 1 is VDO 2
     * (bits 71:40), and so on up to index 5, VDO 6 (bits 199:168). Returns 0 for
     * index 6 and above. The word is returned whether or not the VDO is within
     * numberValidVdos(); the TRM does not say that unused VDOs read zero.
     */
    uint32_t vdo(uint8_t index) const;

    /// @brief ID Header VDO, TRM "RX ID SOP VDO 1"; same as vdo(0)
    uint32_t idHeaderVdo() const { return vdo(0); }
    /// @brief Cert Stat VDO, TRM "RX ID SOP VDO 2"; same as vdo(1)
    uint32_t certStatVdo() const { return vdo(1); }
    /// @brief Product VDO, TRM "RX ID SOP VDO 3"; same as vdo(2)
    uint32_t productVdo() const { return vdo(2); }

    /**
     * @brief Print the response type, the count, and the valid VDOs as hex
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: Number Valid VDOs is at most 6 and reserved bits 5:3 read 0.
     * VDOs beyond the count and the count against the Response Type are not
     * checked, because the TRM states neither rule.
     */
    bool isSemanticallyValid() const override;

    static constexpr uint8_t kReservedMask = 0x38;  ///< byte 0 bits 5:3
};

/// @brief TRM text for a Response Type value
const char *toString(TPS25751ReceivedSOPIdentity::ResponseType val);
