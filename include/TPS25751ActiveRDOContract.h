#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Rdo.h"

/**
 * @brief TPS25751 Active RDO Contract Register (0x35)
 *
 * This register stores the RDO (Request Data Object) of the current explicit
 * USB PD contract, or all zeroes if no contract exists.
 *
 * Layout (SLVUCR8B (Rev. B, 2026-08), page 39):
 * - Bits 31:0    The RDO this device sent as a Sink (rawOwnRdo(), ownRdo())
 * - Bits 95:32   Reserved
 * - Bits 127:96  RequestedRDO (rawRequestedRdo(), requestedRdo())
 *
 * An RDO's layout depends on the kind of PDO it requests, which the word does not
 * say (USB PD R3.3 Tables 6.19 to 6.22), so the typed accessors take an RdoKind.
 * For bits 31:0 the kind comes from the active PDO: rdoKindFor(activePdo()) of
 * ACTIVE_PDO_CONTRACT (0x34). For bits 127:96 it comes from this device's own
 * advertised PDO at that RDO's Object Position in TRANSMIT_SOURCE_CAPABILITIES (0x32):
 * rdoKindFor(txSourceCaps.pdo(requestedRdo().objectPosition() - 1)). With RdoKind::Unknown
 * only the common fields (Object Position and the flags) decode.
 *
 * Hardware reading (board sinking on a 20 V Fixed contract, two chargers,
 * 2026-11-19 and 2026-11-22): RequestedRDO read the same word as bits 31:0
 * (0x4184B12C, then 0x4104B12C), although the TRM describes it as the RDO a
 * partner sends while this device is source.
 *
 * Register Size: 16 bytes
 * Access: Read-only
 * Address: 0x35
 */
class TPS25751ActiveRDOContract : public TPS25751Register
{
public:
    explicit TPS25751ActiveRDOContract() : TPS25751Register(TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT.size) {}
    explicit TPS25751ActiveRDOContract(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT.size) {}
    TPS25751ActiveRDOContract(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ActiveRDOContract(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief The RDO sent as Sink, bits 31:0, as read
    uint32_t rawOwnRdo() const;

    /// @brief The RequestedRDO, bits 127:96, as read; 0 if the buffer is too short
    uint32_t rawRequestedRdo() const;

    /**
     * @brief The RDO this device sent as a Sink (bits 31:0), decoded as @p kind
     *
     * The kind follows the active PDO: pass rdoKindFor(activePdo()) of 0x34.
     */
    TPS25751Rdo ownRdo(RdoKind kind = RdoKind::Unknown) const;

    /**
     * @brief The RequestedRDO (bits 127:96), decoded as @p kind
     *
     * Per the TRM, the RDO a partner sent while this device is the Source, updated
     * once the Accept message has been sent and tSrcTransition has expired (SLVUCR8B
     * page 39). It means a partner's request only while STATUS (0x1A) reports the
     * port role as Source. On hardware, with the board sinking, it read the same word
     * as bits 31:0 with two chargers.
     *
     * The kind comes from this device's own advertised PDO at the RDO's Object
     * Position in TRANSMIT_SOURCE_CAPABILITIES (0x32):
     * rdoKindFor(txSourceCaps.pdo(requestedRdo().objectPosition() - 1)).
     *
     * TI names the deprecated field at bits 9:0 of a Fixed, Variable, or Battery RDO
     * "Max Min Operation Current"; the specification calls it Maximum Operating
     * Current (or Power), a copy of the operating value (maximumOperatingCurrent_mA()).
     */
    TPS25751Rdo requestedRdo(RdoKind kind = RdoKind::Unknown) const;

    /**
     * @brief Check if contract exists
     * @return true if an active RDO contract exists (bits 31:0 non-zero)
     */
    bool hasActiveContract() const;

    using TPS25751Register::debugPrint;

    /// @brief Print both RDOs with their kind unknown (common fields only)
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Print both RDOs, each decoded with its kind
     * @param s             Output stream
     * @param ownKind       Kind of bits 31:0, from rdoKindFor(activePdo()) of 0x34
     * @param requestedKind Kind of bits 127:96, from the matching PDO in 0x32
     */
    void debugPrint(Stream &s, RdoKind ownKind, RdoKind requestedKind) const;

    /**
     * @brief Validates active RDO contract register data semantically
     *
     * A nonzero RDO in bits 31:0 or 127:96 must have an Object Position of 1 to 7.
     * @return true if RDO contract data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
