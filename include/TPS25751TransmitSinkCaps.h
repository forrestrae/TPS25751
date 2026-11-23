#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Pdo.h"

/**
 * @file TPS25751TransmitSinkCaps.h
 * @brief TRANSMIT_SINK_CAPABILITIES register (0x33) class
 *
 * @brief TPS25751 Transmit Sink Capabilities Register (0x33)
 *
 * This register holds the Sink Capabilities message the PD controller
 * ADVERTISES to a port partner -- i.e. the outbound counterpart of
 * TPS25751ReceivedSinkCaps (0x31), which decodes what a partner sent to us.
 * Contains up to 7 Power Data Objects (PDOs). This register is initialized
 * by Application Customization; writes take effect the next time the
 * controller sends a Sink Capabilities message, not immediately. Unlike
 * TRANSMIT_SOURCE_CAPABILITIES, this register has no Power Path bytes -- the
 * layout is identical in shape to TPS25751ReceivedSinkCaps.
 *
 * Register layout (little-endian PDOs):
 * - Byte 0, bits 2:0: Number Valid PDOs (0-7); bits 7:3 reserved
 * - Bytes 1..24: PDO 1..7, 4 bytes each, little-endian (PDO n at byte 1 + (n-1)*4)
 *
 * This is the library's first PDO ENCODE path (alongside
 * TPS25751TransmitSourceCaps): in addition to pdo(), this class provides pure
 * static PDO word builders (on TPS25751Pdo::encode*()) (buildFixedSupplyPDO/buildBatteryPDO/buildVariablePDO/
 * buildPPSPDO) and instance mutators (setPDO/setPDOCount/setFixedSupplyPDO/
 * ...) that build on the base class's setBits() read-modify-write primitive.
 *
 * Register Details:
 * - Address: 0x33
 * - Size: 53 bytes
 * - Access: Read/Write
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), Section 3.2.18, Table 3-20, p.37
 * @see USB Power Delivery Specification 3.1, Section 6.4.1 (Power Data Objects)
 */
class TPS25751TransmitSinkCaps : public TPS25751Register
{
public:
    explicit TPS25751TransmitSinkCaps() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES.size) {}
    explicit TPS25751TransmitSinkCaps(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES.size) {}
    TPS25751TransmitSinkCaps(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitSinkCaps(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_SINK_CAPABILITIES;

    // --- Decode (through TPS25751Pdo, as TPS25751ReceivedSinkCaps) ---------

    /**
     * @brief Get number of valid PDOs
     * @return PDO count (0-7)
     */
    uint8_t pdoCount() const;

    /**
     * @brief Get a specific PDO by index
     * @param index PDO index (0-6)
     * @return 32-bit PDO value, or 0 if index is out of range
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
     * @brief Check if sink capabilities are configured
     * @return true if at least one PDO is present (PDO count > 0 and PDO 1 is non-zero)
     */
    bool hasSinkCaps() const;

    // --- Encode: pure static PDO word builders ------------------------------

    /**
     * @brief Build a Fixed Supply PDO word (Sink variant, type 00)
     * @param voltage_mV Voltage, exact multiples of 50 mV are lossless
     * @param maxCurrent_mA Max current, exact multiples of 10 mA are lossless
     * @param dualRolePower Dual-Role Power capable
     * @param higherCapability Higher Capability (Sink-only field; bit 28)
     * @param unconstrainedPower Unconstrained Power
     * @param usbCommCapable USB Communications Capable
     * @param dualRoleData Dual-Role Data capable
     * @param fastRoleSwapCurrent Fast Role Swap Required Current (2-bit field, bits 24:23;
     *        see FastRoleSwapCurrent)
     * @return Encoded 32-bit PDO word (type bits 31:30 = 00)
     */
    static uint32_t buildFixedSupplyPDO(uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                         bool dualRolePower = false, bool higherCapability = false,
                                         bool unconstrainedPower = false, bool usbCommCapable = false,
                                         bool dualRoleData = false, uint8_t fastRoleSwapCurrent = 0);

    /**
     * @brief Build a Battery Supply PDO word (type 01)
     * @param maxVoltage_mV Max voltage, exact multiples of 50 mV are lossless
     * @param minVoltage_mV Min voltage, exact multiples of 50 mV are lossless
     * @param maxPower_mW Max power, exact multiples of 250 mW are lossless
     * @return Encoded 32-bit PDO word (type bits 31:30 = 01)
     */
    static uint32_t buildBatteryPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW);

    /**
     * @brief Build a Variable Supply PDO word (type 10)
     * @param maxVoltage_mV Max voltage, exact multiples of 50 mV are lossless
     * @param minVoltage_mV Min voltage, exact multiples of 50 mV are lossless
     * @param maxCurrent_mA Max current, exact multiples of 10 mA are lossless
     * @return Encoded 32-bit PDO word (type bits 31:30 = 10)
     */
    static uint32_t buildVariablePDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA);

    /**
     * @brief Build an Augmented/PPS PDO word (Sink variant, type 11, SPR PPS subtype)
     *
     * Sets bits 31:30 = 11 and bits 29:28 = 00 (SPR PPS APDO subtype). Unlike
     * the Source variant, the Sink PPS PDO has no Power Limited bit.
     *
     * @param maxVoltage_mV Max voltage, exact multiples of 100 mV are lossless
     * @param minVoltage_mV Min voltage, exact multiples of 100 mV are lossless
     * @param maxCurrent_mA Max current, exact multiples of 50 mA are lossless
     * @return Encoded 32-bit PDO word
     */
    static uint32_t buildPPSPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA);

    // --- Encode: instance mutators ------------------------------------------

    /**
     * @brief Set the Number Valid PDOs field (bits 2:0 of byte 0)
     * @param count PDO count (masked to 3 bits; values > 7 wrap)
     */
    void setPDOCount(uint8_t count);

    /**
     * @brief Write a raw 32-bit PDO word at the given index
     *
     * No-op if the register is invalid, @p index >= 7, or the buffer is too
     * small to hold the PDO at its offset.
     *
     * @param index PDO index (0-6)
     * @param pdo 32-bit PDO word (little-endian on the wire)
     */
    void setPDO(uint8_t index, uint32_t pdo);

    /// @brief Build and write a Fixed Supply PDO at @p index. @see buildFixedSupplyPDO
    void setFixedSupplyPDO(uint8_t index, uint16_t voltage_mV, uint16_t maxCurrent_mA,
                            bool dualRolePower = false, bool higherCapability = false,
                            bool unconstrainedPower = false, bool usbCommCapable = false,
                            bool dualRoleData = false, uint8_t fastRoleSwapCurrent = 0);

    /// @brief Build and write a Battery PDO at @p index. @see buildBatteryPDO
    void setBatteryPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW);

    /// @brief Build and write a Variable Supply PDO at @p index. @see buildVariablePDO
    void setVariablePDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA);

    /// @brief Build and write an Augmented/PPS PDO at @p index. @see buildPPSPDO
    void setPPSPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA);

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates transmit sink capabilities register data semantically
     *
     * Applies validatePdoList() with PdoListRules::TransmitSink: the receiver rules,
     * the PDO order, and no SPR AVS APDO (a Sink Shall Not send one).
     * @return true if the configured sink capabilities data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
