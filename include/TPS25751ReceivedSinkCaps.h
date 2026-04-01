#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751ActivePDOContract.h"  // For PDOType enum

/**
 * @brief TPS25751 Received Sink Capabilities Register (0x31)
 *
 * This register stores the latest Sink Capabilities message received over BMC.
 * Contains up to 7 Power Data Objects (PDOs) from the sink device.
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
     * @brief Get PDO type for a specific PDO
     * @param index PDO index (0-6)
     * @return PDO type
     */
    PDOType getPDOType(uint8_t index) const;

    /**
     * @brief Check if sink capabilities were received
     * @return true if at least one PDO is present
     */
    bool hasSinkCaps() const;

    // Helper methods to parse Fixed Supply PDO (type 00)
    uint16_t getFixedVoltage_mV(uint8_t index) const;
    uint16_t getFixedMaxCurrent_mA(uint8_t index) const;
    bool getFixedDualRolePower(uint8_t index) const;
    bool getFixedHigherCapability(uint8_t index) const;
    bool getFixedUnconstrainedPower(uint8_t index) const;
    bool getFixedUSBCommCapable(uint8_t index) const;
    bool getFixedDualRoleData(uint8_t index) const;
    bool getFixedFastRoleSwapCurrent(uint8_t index) const;

    // Helper methods to parse Battery PDO (type 01)
    uint16_t getBatteryMaxVoltage_mV(uint8_t index) const;
    uint16_t getBatteryMinVoltage_mV(uint8_t index) const;
    uint32_t getBatteryMaxPower_mW(uint8_t index) const;

    // Helper methods to parse Variable Supply PDO (type 10)
    uint16_t getVariableMaxVoltage_mV(uint8_t index) const;
    uint16_t getVariableMinVoltage_mV(uint8_t index) const;
    uint16_t getVariableMaxCurrent_mA(uint8_t index) const;

    // Helper methods to parse Augmented/PPS PDO (type 11)
    uint16_t getPPSMaxVoltage_mV(uint8_t index) const;
    uint16_t getPPSMinVoltage_mV(uint8_t index) const;
    uint16_t getPPSMaxCurrent_mA(uint8_t index) const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates received sink capabilities register data semantically
     * @return true if sink capabilities data is logically consistent
     */
    bool isSemanticallyValid() const override;

private:
    /**
     * @brief Extract bits from a specific PDO
     * @param index PDO index (0-6)
     * @param bitOffset Bit offset within the PDO
     * @param bitCount Number of bits to extract
     * @return Extracted value
     */
    uint32_t extractPDOBits(uint8_t index, uint8_t bitOffset, uint8_t bitCount) const;
};
