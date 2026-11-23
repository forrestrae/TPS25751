#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Pdo.h"

/**
 * @brief TPS25751 Received Sink Capabilities Register (0x31)
 *
 * This register stores the latest Sink Capabilities message received over BMC.
 * Contains up to 7 Power Data Objects (PDOs) from the sink device, decoded
 * through TPS25751Pdo (USB PD R3.3) by pdo(). The deprecated SPR AVS Sink APDO
 * is identified (PdoKind::SprAvs) but not decoded, and does not fail validation.
 *
 * Register Size: 53 bytes (29 bytes data + 24 bytes reserved)
 * Access: Read-only
 * Address: 0x31
 */
class TPS25751ReceivedSinkCaps : public TPS25751Register
{
public:
    explicit TPS25751ReceivedSinkCaps() : TPS25751Register(TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES.size) {}
    explicit TPS25751ReceivedSinkCaps(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES.size) {}
    TPS25751ReceivedSinkCaps(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ReceivedSinkCaps(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get number of valid PDOs
     * @return PDO count (0-7)
     */
    uint8_t pdoCount() const;

    /**
     * @brief Get a specific PDO by index
     * @param index PDO index (0-6)
     * @return 32-bit PDO value
     */
    uint32_t getPDO(uint8_t index) const;

    /**
     * @brief Decode one PDO in its position and role
     *
     * PDO @p index is PDO @p index + 1 of a Sink_Capabilities message, so the Fixed PDO flags
     * decode only for index 0 (USB PD R3.3 Tables 6.9, 6.10).
     *
     * @param index PDO index (0-6)
     * @return The PDO as a Sink PDO; an all-zero PDO at an unknown position when
     *         @p index is 7 or more
     */
    TPS25751Pdo pdo(uint8_t index) const;

    /**
     * @brief Check if sink capabilities were received
     * @return true if at least one PDO is present
     */
    bool hasSinkCaps() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates received sink capabilities register data semantically
     *
     * Applies validatePdoList() with PdoListRules::ReceivedSink.
     * @return true if sink capabilities data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
