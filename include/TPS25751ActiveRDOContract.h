#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Active RDO Contract Register (0x35)
 *
 * This register stores the RDO (Request Data Object) of the current explicit
 * USB PD contract, or all zeroes if no contract exists.
 *
 * Layout (SLVUCR8B (Rev. B, 2026-08), page 39):
 * - Bits 31:0    RDO of the active contract (decoded by the accessors below)
 * - Bits 95:32   Reserved
 * - Bits 127:96  RequestedRDO: the RDO the port partner requested while the
 *                PD controller is the source (see requestedRDO())
 *
 * The accessors decode bits 31:0 as a Fixed and Variable RDO (USB PD R3.3 Table
 * 6.19, page 142), which is the TRM's field layout. The RDO's format follows the
 * PDO at objectPosition() in the Source_Capabilities (0x30), so for other
 * contracts the lower fields mean something else: in a Battery RDO (Table 6.20)
 * bits 19:10 are Operating Power in 250 mW units, and in a PPS or AVS RDO (Tables
 * 6.21, 6.22) bits 20:9 are Output Voltage and bits 6:0 Operating Current in 50 mA
 * units. TRM reserved bit 22 is EPR Capable in every RDO. ACTIVE_PDO_CONTRACT
 * (0x34) pdoType() and apdoSubtype() tell which applies.
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

    /**
     * @brief Get object position (which PDO from 1-7)
     * @return Object position (1-7)
     */
    uint8_t objectPosition() const;

    /**
     * @brief Get GiveBack flag
     * @return true if GiveBack flag is set
     */
    bool giveBackFlag() const;

    /**
     * @brief Get capability mismatch flag (bit 26)
     *
     * TRM: "Capabality Missmatch" (SLVUCR8B p.39; TI's spelling).
     * @return true if capability mismatch detected
     */
    bool capabilityMismatch() const;

    /**
     * @brief Get USB communication capable flag
     * @return true if USB communication capable
     */
    bool usbCommCapable() const;

    /**
     * @brief Get No USB Suspend flag
     * @return true if no USB suspend
     */
    bool noUSBSuspend() const;

    /**
     * @brief Get unchunked messages supported flag
     * @return true if unchunked extended messages supported
     */
    bool unchunkedSupported() const;

    /**
     * @brief Get operating current/power
     * @return Operating current in 10mA units (or power in 250mW units for Battery PDO)
     */
    uint16_t operatingCurrent() const;

    /**
     * @brief Get operating current in milliamps (assumes Fixed/Variable PDO)
     * @return Operating current in mA
     */
    uint16_t operatingCurrent_mA() const;

    /**
     * @brief Get max/min operating current
     * @return Max or min operating current in 10mA units (or power in 250mW units)
     */
    uint16_t maxMinCurrent() const;

    /**
     * @brief Get max/min operating current in milliamps (assumes Fixed/Variable PDO)
     * @return Max/min current in mA
     */
    uint16_t maxMinCurrent_mA() const;

    /**
     * @brief Check if contract exists
     * @return true if an active RDO contract exists (bits 31:0 non-zero)
     */
    bool hasActiveContract() const;

    /**
     * @brief Get the RDO requested by the port partner (bits 127:96)
     *
     * Valid when the PD controller is acting as a source. Updated when a Source
     * PDO has been negotiated (the Accept message has been transmitted and the
     * tSrcTransition timer has expired). Source: SLVUCR8B (Rev. B, 2026-08),
     * page 39.
     *
     * @return Raw 32-bit RequestedRDO word; 0 if the buffer is too short
     */
    uint32_t requestedRDO() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates active RDO contract register data semantically
     * @return true if RDO contract data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
