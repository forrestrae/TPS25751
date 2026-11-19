#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief PDO (Power Data Object) Type
 */
enum class PDOType : uint8_t {
    FixedSupply = 0b00,     ///< Fixed voltage supply
    Battery = 0b01,         ///< Battery
    VariableSupply = 0b10,  ///< Variable voltage supply
    Augmented = 0b11        ///< Augmented PDO (APDO); see apdoSubtype()
};

/// APDO subtype (bits 29:28 of an Augmented PDO) of the SPR PPS APDO, the only
/// subtype this library decodes.
constexpr uint8_t kApdoSubtypeSprPps = 0b00;

/**
 * @brief APDO subtype of a PDO word (bits 29:28)
 *
 * Meaningful only when bits 31:30 are 11b (PDOType::Augmented). Subtype 00b is
 * the SPR PPS APDO. USB PD 3.1 and later use the other values for further APDO
 * kinds, which this library does not decode yet. A TPS25751 test board's
 * TRANSMIT_SOURCE_CAPABILITIES (0x32) carries a subtype 10b APDO, observed on
 * hardware on 2026-11-18.
 */
inline uint8_t apdoSubtype(uint32_t pdo)
{
    return static_cast<uint8_t>((pdo >> 28) & 0x3);
}

/**
 * @brief True when a PDO word is an SPR PPS APDO (bits 31:30 = 11b, bits 29:28 = 00b)
 *
 * The PPS accessors of every PDO class decode only such PDOs; they return 0 for
 * an Augmented PDO of any other subtype, whose fields have a different layout.
 */
inline bool isSprPpsApdo(uint32_t pdo)
{
    return ((pdo >> 30) & 0x3) == static_cast<uint32_t>(PDOType::Augmented) &&
           apdoSubtype(pdo) == kApdoSubtypeSprPps;
}

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
     * @return 32-bit PDO value
     */
    uint32_t activePDO() const;

    /**
     * @brief Get First PDO Control Bits (register bits 41:32)
     *
     * PDO-specific information drawn from the first PDO, whichever PDO was
     * selected. Source: SLVUCR8B (Rev. B, 2026-08), page 38.
     *
     * @return 10-bit control value
     */
    uint16_t firstPDOControlBits() const;

    /**
     * @brief Get PDO type
     * @return PDO type (Fixed, Battery, Variable, Augmented)
     */
    PDOType pdoType() const;

    /**
     * @brief Check if contract exists
     * @return true if an active PD contract exists
     */
    bool hasActiveContract() const;

    // Fixed Supply PDO fields (type 00)
    uint16_t fixedVoltage_mV() const;          ///< Voltage in millivolts (50mV units)
    uint16_t fixedMaxCurrent_mA() const;       ///< Max current in milliamps (10mA units)
    bool fixedDualRolePower() const;
    bool fixedUSBSuspendSupported() const;
    bool fixedUnconstrainedPower() const;
    bool fixedUSBCommCapable() const;
    bool fixedDualRoleData() const;
    bool fixedUnchunkedSupported() const;

    // Battery PDO fields (type 01)
    uint16_t batteryMaxVoltage_mV() const;     ///< Max voltage in millivolts (50mV units)
    uint16_t batteryMinVoltage_mV() const;     ///< Min voltage in millivolts (50mV units)
    uint32_t batteryMaxPower_mW() const;       ///< Max power in milliwatts (250mW units)

    // Variable Supply PDO fields (type 10)
    uint16_t variableMaxVoltage_mV() const;    ///< Max voltage in millivolts (50mV units)
    uint16_t variableMinVoltage_mV() const;    ///< Min voltage in millivolts (50mV units)
    uint16_t variableMaxCurrent_mA() const;    ///< Max current in milliamps (10mA units)

    // Augmented PDO fields (type 11). The pps*() accessors decode only the SPR
    // PPS APDO (subtype 00b) and return 0/false for any other subtype.
    uint8_t apdoSubtype() const;               ///< APDO subtype, bits 29:28 (meaningful for Augmented only)
    bool isSprPps() const;                     ///< True when the active PDO is an SPR PPS APDO
    bool ppsPowerLimited() const;
    uint16_t ppsMaxVoltage_mV() const;         ///< Max voltage in millivolts (100mV units for PPS)
    uint16_t ppsMinVoltage_mV() const;         ///< Min voltage in millivolts (100mV units for PPS)
    uint16_t ppsMaxCurrent_mA() const;         ///< Max current in milliamps (50mA units for PPS)

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates active PDO contract register data semantically
     * @return true if PDO contract data is logically consistent
     */
    bool isSemanticallyValid() const override;
};

// String conversion helper
const char* toString(PDOType type);
