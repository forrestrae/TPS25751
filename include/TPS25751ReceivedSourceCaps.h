#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Pdo.h"

/**
 * @brief TPS25751 Received Source Capabilities Register (0x30)
 *
 * This register stores the latest Source Capabilities message received over BMC.
 * Contains up to 7 Power Data Objects (PDOs) from the source device, decoded
 * through TPS25751Pdo (USB PD R3.3) by pdo().
 *
 * Register Size: 53 bytes (29 bytes data + 24 bytes reserved)
 * Access: Read-only
 * Address: 0x30
 */
class TPS25751ReceivedSourceCaps : public TPS25751Register
{
public:
    explicit TPS25751ReceivedSourceCaps() : TPS25751Register(TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES.size) {}
    explicit TPS25751ReceivedSourceCaps(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES.size) {}
    TPS25751ReceivedSourceCaps(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ReceivedSourceCaps(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

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
     * PDO @p index is PDO @p index + 1 of a Source_Capabilities message, so the
     * Fixed PDO flags (Dual-Role Power, Unconstrained Power, and so on) decode only
     * for index 0 (USB PD R3.3 Tables 6.8, 6.10).
     *
     * @param index PDO index (0-6)
     * @return The PDO as a Source PDO; an all-zero PDO at an unknown position when
     *         @p index is 7 or more
     */
    TPS25751Pdo pdo(uint8_t index) const;

    /**
     * @brief Check if source capabilities were received
     * @return true if at least one PDO is present
     */
    bool hasSourceCaps() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates received source capabilities register data semantically
     *
     * Applies validatePdoList() with PdoListRules::ReceivedSource: each PDO must
     * pass TPS25751Pdo::isValidForReceiver() and PDO 1 must be the 5 V Fixed PDO.
     * @return true if source capabilities data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
