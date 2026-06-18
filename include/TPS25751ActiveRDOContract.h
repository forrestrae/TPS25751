#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Active RDO Contract Register (0x35)
 *
 * This register stores the RDO (Request Data Object) of the current explicit
 * USB PD contract, or all zeroes if no contract exists.
 *
 * Register Size: 4 bytes
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
     * @brief Get capability mismatch flag
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
     * @return true if an active RDO contract exists
     */
    bool hasActiveContract() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates active RDO contract register data semantically
     * @return true if RDO contract data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
