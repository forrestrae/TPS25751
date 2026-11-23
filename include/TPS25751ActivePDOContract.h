#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Pdo.h"

/**
 * @brief TPS25751 Active PDO Contract Register (0x34)
 *
 * This register stores PDO data for the current explicit USB PD contract,
 * or all zeroes if no contract exists.
 *
 * Layout (SLVUCR8B (Rev. B, 2026-08), page 38): bits 31:0 Active PDO,
 * bits 41:32 First PDO Control Bits, bits 47:42 reserved.
 *
 * Register Size: 6 bytes
 * Access: Read-only
 * Address: 0x34
 */
class TPS25751ActivePDOContract : public TPS25751Register
{
public:
    explicit TPS25751ActivePDOContract() : TPS25751Register(TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT.size) {}
    explicit TPS25751ActivePDOContract(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT.size) {}
    TPS25751ActivePDOContract(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ActivePDOContract(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /**
     * @brief Get the full Active PDO value (bits 31:0)
     * @return 32-bit PDO word as read
     */
    uint32_t rawActivePdo() const;

    /**
     * @brief Get First PDO Control Bits (register bits 41:32) as read
     *
     * PDO-specific information drawn from the first PDO, whichever PDO was
     * selected. Source: SLVUCR8B (Rev. B, 2026-08), page 38. firstPdoControl()
     * decodes it.
     *
     * @return 10-bit control value
     */
    uint16_t rawFirstPdoControlBits() const;

    /**
     * @brief The active PDO, decoded
     *
     * The PDO's position in the Source_Capabilities is not known here, so its
     * position is TPS25751Pdo::kUnknownPosition and the Fixed PDO flag accessors
     * (Dual-Role Power, Unconstrained Power, and so on) read false: bits 29:22 of a
     * Fixed PDO above 5 V are reserved Device Flags (USB PD R3.3 Table 6.10).
     * Peak Current, voltage, current, PPS, and AVS fields decode. firstPdoControl()
     * carries PDO 1's flags.
     */
    TPS25751Pdo activePdo() const;

    /**
     * @brief PDO 1's flags and Peak Current, from First PDO Control Bits
     *
     * The ten bits are bits 29:20 of PDO 1, the vSafe5V Fixed Supply PDO (USB PD
     * R3.3 Table 6.8, page 132): bit 9 here is Dual-Role Power (PDO bit 29), bit 8 USB
     * Suspend Supported, bit 7 Unconstrained Power, bit 6 USB Communications
     * Capable, bit 5 Dual-Role Data, bit 4 Unchunked Extended Messages Supported,
     * bit 3 EPR Capable, bit 2 reserved, and bits 1:0 Peak Current. The TRM does
     * not state this mapping. On hardware it held with two chargers: 0x098 here
     * with PDO 1 of RECEIVED_SOURCE_CAPABILITIES (0x30) = 0x0981912C, whose bits
     * 29:20 are 0x098 (2026-11-19), and 0x080 here with PDO 1 decoding as
     * Unconstrained Power set (2026-11-22).
     *
     * The returned object is PDO 1 as a Source PDO with only bits 29:20 filled in,
     * so its flag accessors and peakCurrent() give PDO 1's values. Its voltage and
     * current read 0 and must not be used.
     */
    TPS25751Pdo firstPdoControl() const;

    /**
     * @brief Check if contract exists
     * @return true if an active PD contract exists (the raw active PDO is non-zero)
     */
    bool hasActiveContract() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates active PDO contract register data semantically
     *
     * With an active contract, the active PDO must pass TPS25751Pdo::isValidForReceiver().
     * @return true if PDO contract data is logically consistent
     */
    bool isSemanticallyValid() const override;
};

