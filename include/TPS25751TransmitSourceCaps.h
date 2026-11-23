#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Pdo.h"

/**
 * @file TPS25751TransmitSourceCaps.h
 * @brief TRANSMIT_SOURCE_CAPABILITIES register (0x32) class
 *
 * @brief TPS25751 Transmit Source Capabilities Register (0x32)
 *
 * This register holds the Source Capabilities message the PD controller
 * ADVERTISES to a port partner -- i.e. the outbound counterpart of
 * TPS25751ReceivedSourceCaps (0x30), which decodes what a partner sent to
 * us. Contains up to 7 Power Data Objects (PDOs), preceded by a 2-bit Power
 * Path selector for PDO 1 only. This register is initialized by
 * Application Customization; writes take effect the next time the controller
 * sends a Source Capabilities message, not immediately.
 *
 * Register layout (little-endian PDOs):
 * - Byte 0, bits 2:0: Number Valid PDOs (0-7); bits 7:3 reserved
 * - Byte 1, bits 1:0 (register bits 9:8): Power Path for PDO 1
 *   (0h = PP5V, 2h = PP_EXT)
 * - Register bits 23:10 (byte 1 bits 7:2, byte 2): reserved. Rev. A defined
 *   Power Path for PDO 2-7 here; SLVUCR8B (Rev. B, 2026-08), page 36, marks
 *   these bits RESERVED, so the library never writes them.
 * - Bytes 3..30: PDO 1..7, 4 bytes each, little-endian (PDO n at byte 3 + (n-1)*4)
 *
 * This is the library's first PDO ENCODE path: in addition to pdo(), this class provides pure
 * static PDO word builders (buildFixedSupplyPDO/buildBatteryPDO/
 * buildVariablePDO/buildPPSPDO/buildSprAvsPDO) and instance mutators (setPDO/setPDOCount/
 * setFixedSupplyPDO/... /setPowerPathForPDO) that build on the base class's
 * setBits() read-modify-write primitive.
 *
 * Register Details:
 * - Address: 0x32
 * - Size: 63 bytes
 * - Access: Read/Write
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), Section 3.2.17, Table 3-19, p.36
 * @see USB Power Delivery Specification 3.1, Section 6.4.1 (Power Data Objects)
 */
class TPS25751TransmitSourceCaps : public TPS25751Register
{
public:
    explicit TPS25751TransmitSourceCaps() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES.size) {}
    explicit TPS25751TransmitSourceCaps(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES.size) {}
    TPS25751TransmitSourceCaps(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitSourceCaps(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES;

    // --- Decode (through TPS25751Pdo, as TPS25751ReceivedSourceCaps) ------

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
     * PDO @p index is PDO @p index + 1 of a Source_Capabilities message, so the Fixed PDO flags
     * decode only for index 0 (USB PD R3.3 Tables 6.8, 6.10).
     *
     * @param index PDO index (0-6)
     * @return The PDO as a Source PDO; an all-zero PDO at an unknown position when
     *         @p index is 7 or more
     */
    TPS25751Pdo pdo(uint8_t index) const;

    /**
     * @brief Check if source capabilities are configured
     * @return true if at least one PDO is present (PDO count > 0 and PDO 1 is non-zero)
     */
    bool hasSourceCaps() const;

    /**
     * @brief Get the Power Path selector for PDO 1
     *
     * Register bits 9:8: 0h = PP5V, 2h = PP_EXT (1h/3h undefined).
     * Source: SLVUCR8B (Rev. B, 2026-08), page 36. Rev. B defines a Power
     * Path only for PDO 1; bits 23:10, which Rev. A used for PDO 2-7, are
     * reserved.
     *
     * @param index PDO index; only 0 (PDO 1) is meaningful
     * @return Raw 2-bit Power Path selector for index 0; 0 for any other index
     */
    uint8_t getPowerPathForPDO(uint8_t index) const;

    // --- Encode: pure static PDO word builders ------------------------------

    /**
     * @brief Build a Fixed Supply PDO word (Source variant, type 00)
     * @param voltage_mV Voltage, exact multiples of 50 mV are lossless
     * @param maxCurrent_mA Max current, exact multiples of 10 mA are lossless
     * @param dualRolePower Dual-Role Power capable
     * @param usbSuspend USB Suspend Supported
     * @param unconstrainedPower Unconstrained Power
     * @param usbCommCapable USB Communications Capable
     * @param dualRoleData Dual-Role Data capable
     * @param unchunked Unchunked Extended Messages Supported
     * @return Encoded 32-bit PDO word (type bits 31:30 = 00)
     */
    static uint32_t buildFixedSupplyPDO(uint16_t voltage_mV, uint16_t maxCurrent_mA,
                                         bool dualRolePower = false, bool usbSuspend = false,
                                         bool unconstrainedPower = false, bool usbCommCapable = false,
                                         bool dualRoleData = false, bool unchunked = false);

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
     * @brief Build an Augmented/PPS PDO word (Source variant, type 11, SPR PPS subtype)
     *
     * Sets bits 31:30 = 11 and bits 29:28 = 00 (SPR PPS APDO subtype).
     *
     * @param maxVoltage_mV Max voltage, exact multiples of 100 mV are lossless
     * @param minVoltage_mV Min voltage, exact multiples of 100 mV are lossless
     * @param maxCurrent_mA Max current, exact multiples of 50 mA are lossless
     * @param powerLimited PPS Power Limited (Source-only field)
     * @return Encoded 32-bit PDO word
     */
    static uint32_t buildPPSPDO(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA,
                                bool powerLimited = false);

    /**
     * @brief Build an SPR AVS Source APDO word (Table 6.15)
     *
     * @param current15V_mA Maximum current from 9 to 15 V, multiples of 10 mA are lossless
     * @param current20V_mA Maximum current from 15 to 20 V, multiples of 10 mA are lossless
     * @param peak          Peak Current (Table 6.18)
     * @return Encoded 32-bit PDO word; buildSprAvsPDO(3000, 5000) is 0xE004B1F4
     */
    static uint32_t buildSprAvsPDO(uint16_t current15V_mA, uint16_t current20V_mA,
                                   PdoPeakCurrent peak = PdoPeakCurrent::IoC);

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
                            bool dualRolePower = false, bool usbSuspend = false,
                            bool unconstrainedPower = false, bool usbCommCapable = false,
                            bool dualRoleData = false, bool unchunked = false);

    /// @brief Build and write a Battery PDO at @p index. @see buildBatteryPDO
    void setBatteryPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t maxPower_mW);

    /// @brief Build and write a Variable Supply PDO at @p index. @see buildVariablePDO
    void setVariablePDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA);

    /// @brief Build and write an Augmented/PPS PDO at @p index. @see buildPPSPDO
    void setPPSPDO(uint8_t index, uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t maxCurrent_mA,
                   bool powerLimited = false);

    /// @brief Build and write an SPR AVS Source APDO at @p index. @see buildSprAvsPDO
    void setSprAvsPDO(uint8_t index, uint16_t current15V_mA, uint16_t current20V_mA,
                      PdoPeakCurrent peak = PdoPeakCurrent::IoC);

    /**
     * @brief Set the Power Path selector for PDO 1 (register bits 9:8)
     *
     * Source: SLVUCR8B (Rev. B, 2026-08), page 36: 0h = PP5V, 2h = PP_EXT.
     * Rev. B defines a Power Path only for PDO 1 and marks bits 23:10
     * reserved, so this is a no-op for any index other than 0; it never
     * writes bits 23:10.
     *
     * @param index PDO index; only 0 (PDO 1) writes, any other index is a no-op
     * @param pp Raw 2-bit Power Path selector value (masked to 2 bits)
     */
    void setPowerPathForPDO(uint8_t index, uint8_t pp);

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates transmit source capabilities register data semantically
     *
     * Applies validatePdoList() with PdoListRules::TransmitSource: the receiver
     * rules, the PDO order, each Variable PDO's minimum at least 80% of its maximum,
     * and each SPR AVS APDO's currents matching the 15 V and 20 V Fixed PDOs.
     * @return true if the configured source capabilities data is logically consistent
     */
    bool isSemanticallyValid() const override;
};
