#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * @file TPS25751Pdo.h
 * @brief USB PD Power Data Object (PDO) value type shared by registers 0x30 to 0x34
 *
 * RECEIVED_SOURCE_CAPABILITIES (0x30), RECEIVED_SINK_CAPABILITIES (0x31),
 * TRANSMIT_SOURCE_CAPABILITIES (0x32), TRANSMIT_SINK_CAPABILITIES (0x33), and
 * ACTIVE_PDO_CONTRACT (0x34) carry USB PD PDO words. The TRM defers their layout to
 * the USB PD specification, so every register decodes them through this one type,
 * from USB PD R3.3 V1.0 (served by the `usb-pd-docs` MCP server):
 *
 * | Table | Object                              | Kind                        |
 * |-------|-------------------------------------|-----------------------------|
 * | 6.6   | PDO type, bits 31:30                | type()                      |
 * | 6.7   | APDO type, bits 29:28               | apdoType(), kind()          |
 * | 6.8   | Fixed 5V Source PDO (PDO 1)         | PdoKind::Fixed, Source      |
 * | 6.9   | Fixed 5V Sink PDO (PDO 1)           | PdoKind::Fixed, Sink        |
 * | 6.10  | Fixed PDO above 5 V                 | PdoKind::Fixed              |
 * | 6.11  | Battery PDO                         | PdoKind::Battery            |
 * | 6.12  | Variable PDO                        | PdoKind::Variable           |
 * | 6.13  | SPR PPS Source APDO                 | PdoKind::SprPps, Source     |
 * | 6.14  | SPR PPS Sink APDO                   | PdoKind::SprPps, Sink       |
 * | 6.15  | SPR AVS Source APDO                 | PdoKind::SprAvs, Source     |
 * | 6.18  | PDO Peak Current values             | PdoPeakCurrent              |
 *
 * A PDO is decoded in context: its 1-based position in the Capabilities list and its
 * role (sent by a Source or by a Sink). Position matters because bits 29:22 of a
 * Fixed PDO hold the port's flags only in PDO 1, the vSafe5V PDO (Tables 6.8, 6.9);
 * in a Fixed PDO above 5 V they are reserved Device Flags (Table 6.10). Role matters
 * because PDO 1 bit 28 and bits 24:23 differ between Source and Sink, Peak Current is
 * defined only when a Source sends it, and the PPS APDO has a Power Limited bit only
 * in the Source form.
 *
 * Accessors that do not apply to the PDO's kind, role, or position return 0 or
 * false, the convention the register classes already used for PPS fields. Every
 * accessor works on a PDO that the receiver rules call invalid; isValidForReceiver()
 * says whether it is.
 *
 * Not decoded: EPR AVS APDOs (APDO type 01b, Tables 6.16 and 6.17), which an SPR
 * Capabilities Message does not carry and an SPR-only TPS25751 never sends, and the
 * deprecated SPR AVS Sink APDO (APDO type 10b sent by a Sink, page 137), which the
 * library identifies and exposes only as a raw word.
 */

/**
 * @brief PDO type, bits 31:30 (USB PD R3.3 Table 6.6)
 */
enum class PDOType : uint8_t {
    FixedSupply = 0b00,     ///< Fixed voltage supply
    Battery = 0b01,         ///< Battery
    VariableSupply = 0b10,  ///< Variable voltage supply
    Augmented = 0b11        ///< Augmented PDO (APDO); see apdoSubtype()
};

/// @brief Text for a PDOType
const char *toString(PDOType type);

/// APDO type (bits 29:28 of an Augmented PDO) of the SPR PPS APDO (Table 6.7).
constexpr uint8_t kApdoSubtypeSprPps = 0b00;
/// APDO type of the EPR AVS APDO (Table 6.7).
constexpr uint8_t kApdoSubtypeEprAvs = 0b01;
/// APDO type of the SPR AVS APDO (Table 6.7).
constexpr uint8_t kApdoSubtypeSprAvs = 0b10;
/// APDO type 11b, which Table 6.7 marks invalid.
constexpr uint8_t kApdoSubtypeInvalid = 0b11;

/**
 * @brief APDO type of a PDO word, bits 29:28 (USB PD R3.3 Table 6.7)
 *
 * Meaningful only when bits 31:30 are 11b (PDOType::Augmented): 00b SPR PPS, 01b EPR
 * AVS, 10b SPR AVS, 11b invalid.
 */
inline uint8_t apdoSubtype(uint32_t pdo)
{
    return static_cast<uint8_t>((pdo >> 28) & 0x3);
}

/// @brief True when a PDO word is an SPR PPS APDO (bits 31:30 = 11b, bits 29:28 = 00b)
inline bool isSprPpsApdo(uint32_t pdo)
{
    return ((pdo >> 30) & 0x3) == static_cast<uint32_t>(PDOType::Augmented) &&
           apdoSubtype(pdo) == kApdoSubtypeSprPps;
}

/// @brief Who sent the PDO: a Source (Source_Capabilities) or a Sink (Sink_Capabilities)
enum class PdoRole : uint8_t {
    Source = 0,
    Sink = 1
};

/// @brief What a PDO word holds, from its PDO type and APDO type (Tables 6.6, 6.7)
enum class PdoKind : uint8_t {
    Fixed = 0,       ///< Fixed Supply PDO (Tables 6.8 to 6.10)
    Battery = 1,     ///< Battery PDO (Table 6.11)
    Variable = 2,    ///< Variable PDO (Table 6.12)
    SprPps = 3,      ///< SPR PPS APDO (Table 6.13 Source, 6.14 Sink)
    EprAvs = 4,      ///< EPR AVS APDO (Tables 6.16, 6.17); not decoded, unexpected on an SPR port
    SprAvs = 5,      ///< SPR AVS APDO (Table 6.15 Source; deprecated when a Sink sends it)
    InvalidApdo = 6  ///< APDO type 11b, invalid (Table 6.7)
};

/// @brief Text for a PdoKind
const char *toString(PdoKind kind);

/**
 * @brief PDO Peak Current, 2-bit field (USB PD R3.3 Table 6.18, page 141)
 *
 * Carried in bits 21:20 of a Fixed PDO sent by a Source (Tables 6.8, 6.10) and in
 * bits 27:26 of an SPR AVS Source APDO (Table 6.15). Codes 01b to 11b are three
 * increasing overload capabilities, each a set of peak-current, duration, and duty
 * cycle limits relative to IoC that Table 6.18 lists.
 */
enum class PdoPeakCurrent : uint8_t {
    IoC = 0,         ///< 00b: peak current equals IoC (default), or see Source_Capabilities_Extended
    Overload1 = 1,   ///< 01b: up to 150% IoC for 1 ms at 5% duty cycle
    Overload2 = 2,   ///< 10b: up to 200% IoC for 1 ms at 5% duty cycle
    Overload3 = 3    ///< 11b: up to 200% IoC for 1 ms at 5% duty cycle, 150% for 10 ms at 50%
};

/// @brief Text for a PdoPeakCurrent, as in Table 6.18
const char *toString(PdoPeakCurrent peak);

/**
 * @brief Fast Role Swap required USB Type-C Current, Sink PDO 1 bits 24:23 (Table 6.9)
 */
enum class FastRoleSwapCurrent : uint8_t {
    NotSupported = 0,  ///< 00b: Fast Role Swap not supported (default)
    DefaultUsb = 1,    ///< 01b: Default USB Port
    Current1_5A = 2,   ///< 10b: 1.5 A at 5 V
    Current3_0A = 3    ///< 11b: 3.0 A at 5 V
};

/// @brief Text for a FastRoleSwapCurrent
const char *toString(FastRoleSwapCurrent current);

/**
 * @brief One USB PD PDO word, decoded in its position and role
 */
class TPS25751Pdo {
public:
    /// Position for a PDO whose place in the Capabilities list is not known (0x34).
    static constexpr uint8_t kUnknownPosition = 0;

    /// Largest Fixed or Variable current code, 500 x 10 mA = 5 A (Tables 6.8 to 6.12).
    static constexpr uint16_t kMaxCurrent10mA = 500;
    /// Largest Battery power code, 400 x 250 mW = 100 W (Table 6.11).
    static constexpr uint16_t kMaxBatteryPower250mW = 400;
    /// Largest PPS current code, 100 x 50 mA = 5 A (Tables 6.13, 6.14).
    static constexpr uint8_t kMaxPpsCurrent50mA = 100;
    /// The vSafe5V voltage code, 100 x 50 mV (Tables 6.8, 6.9).
    static constexpr uint16_t kVSafe5VCode = 100;

    /// An all-zero Source PDO at an unknown position.
    TPS25751Pdo() : word_(0), position_(kUnknownPosition), role_(PdoRole::Source) {}

    /**
     * @param word     The 32-bit PDO word
     * @param position 1-based position in the Capabilities list (1 to 7), or
     *                 kUnknownPosition when the register does not say
     * @param role     Who sent it: PdoRole::Source or PdoRole::Sink
     */
    TPS25751Pdo(uint32_t word, uint8_t position, PdoRole role)
        : word_(word), position_(position), role_(role) {}

    uint32_t raw() const { return word_; }        ///< The PDO word as read
    uint8_t position() const { return position_; }  ///< 1-based position, 0 when unknown
    PdoRole role() const { return role_; }        ///< Who sent it
    bool isFirst() const { return position_ == 1; }  ///< True for PDO 1, the vSafe5V PDO

    PDOType type() const;     ///< PDO type, bits 31:30 (Table 6.6)
    uint8_t apdoType() const; ///< APDO type, bits 29:28 (Table 6.7); 0 unless type() is Augmented
    PdoKind kind() const;     ///< What the word holds, from type() and apdoType()

    // --- Fixed Supply PDO, any position (Tables 6.8 to 6.10) ---------------------

    uint16_t rawFixedVoltage() const;  ///< Bits 19:10, 50 mV units; 0 unless kind() is Fixed
    uint16_t fixedVoltage_mV() const;  ///< Voltage in mV
    /// Bits 9:0, 10 mA units: Maximum Current (Source), Operational Current (Sink); 0 unless Fixed
    uint16_t rawFixedCurrent() const;
    uint16_t fixedCurrent_mA() const;  ///< Current in mA

    // --- Fixed PDO 1 flags (Tables 6.8, 6.9) -------------------------------------
    // Each returns false (or the 0 code) unless the PDO is a Fixed PDO at position 1:
    // in a Fixed PDO above 5 V, bits 29:22 are reserved Device Flags (Table 6.10).

    bool dualRolePower() const;              ///< Bit 29, Source and Sink
    bool usbSuspendSupported() const;        ///< Bit 28, Source only (Table 6.8)
    bool higherCapability() const;           ///< Bit 28, Sink only (Table 6.9)
    bool unconstrainedPower() const;         ///< Bit 27, Source and Sink
    bool usbCommunicationsCapable() const;   ///< Bit 26, Source and Sink
    bool dualRoleData() const;               ///< Bit 25, Source and Sink
    bool unchunkedExtendedMessages() const;  ///< Bit 24, Source only (Table 6.8)
    bool eprCapable() const;                 ///< Bit 23, Source only (Table 6.8)
    FastRoleSwapCurrent fastRoleSwapCurrent() const;  ///< Bits 24:23, Sink only (Table 6.9)
    uint8_t rawFastRoleSwapCurrent() const;           ///< Bits 24:23 as read, Sink only

    // --- Fixed PDO Peak Current, any position, Source only (Tables 6.8, 6.10, 6.18) ---

    uint8_t rawPeakCurrent() const;          ///< Bits 21:20; 0 unless a Fixed PDO sent by a Source
    PdoPeakCurrent peakCurrent() const;      ///< Bits 21:20 as a PdoPeakCurrent

    // --- Battery PDO (Table 6.11) --------------------------------------------------

    uint16_t rawBatteryMaxVoltage() const;   ///< Bits 29:20, 50 mV units; 0 unless Battery
    uint16_t batteryMaxVoltage_mV() const;
    uint16_t rawBatteryMinVoltage() const;   ///< Bits 19:10, 50 mV units
    uint16_t batteryMinVoltage_mV() const;
    uint16_t rawBatteryPower() const;        ///< Bits 9:0, 250 mW units: maximum (Source) or operational (Sink) power
    uint32_t batteryPower_mW() const;

    // --- Variable PDO (Table 6.12) -------------------------------------------------

    uint16_t rawVariableMaxVoltage() const;  ///< Bits 29:20, 50 mV units; 0 unless Variable
    uint16_t variableMaxVoltage_mV() const;
    uint16_t rawVariableMinVoltage() const;  ///< Bits 19:10, 50 mV units
    uint16_t variableMinVoltage_mV() const;
    uint16_t rawVariableCurrent() const;     ///< Bits 9:0, 10 mA units: maximum (Source) or operational (Sink) current
    uint16_t variableCurrent_mA() const;

    // --- SPR PPS APDO (Table 6.13 Source, 6.14 Sink) --------------------------------

    uint8_t rawPpsMaxVoltage() const;        ///< Bits 24:17, 100 mV units; 0 unless SprPps
    uint16_t ppsMaxVoltage_mV() const;
    uint8_t rawPpsMinVoltage() const;        ///< Bits 15:8, 100 mV units
    uint16_t ppsMinVoltage_mV() const;
    uint8_t rawPpsCurrent() const;           ///< Bits 6:0, 50 mA units
    uint16_t ppsCurrent_mA() const;
    bool ppsPowerLimited() const;            ///< Bit 27, Source only (Table 6.13); the Sink APDO reserves it

    // --- SPR AVS Source APDO (Table 6.15) -------------------------------------------
    // Each returns 0 unless kind() is SprAvs and the role is Source; the deprecated
    // SPR AVS Sink APDO is exposed only through raw().

    uint8_t rawAvsPeakCurrent() const;       ///< Bits 27:26 (Table 6.18 values)
    PdoPeakCurrent avsPeakCurrent() const;
    uint16_t rawAvsCurrent15V() const;       ///< Bits 19:10, 10 mA units: maximum current from 9 to 15 V
    uint16_t avsCurrent15V_mA() const;
    uint16_t rawAvsCurrent20V() const;       ///< Bits 9:0, 10 mA units: maximum current from 15 to 20 V
    uint16_t avsCurrent20V_mA() const;

    /**
     * @brief True unless the PDO holds a value the tables tell a receiver to treat
     *        as invalid, or a zero voltage
     *
     * Rejects: APDO type 11b (Table 6.7); a Fixed or Variable current above 500
     * (Tables 6.8 to 6.10, 6.12); a Battery power above 400 (Table 6.11); a PPS
     * current above 100 (Tables 6.13, 6.14); a Source PPS maximum voltage other than
     * 110, 160, or 210, or minimum other than 33 or 50 (Table 6.13); a Sink PPS
     * voltage outside 50 to 210 (Table 6.14); an SPR AVS Source current above 500
     * (Table 6.15); and a zero voltage in a Fixed, Battery, or Variable PDO. Zero
     * currents and powers are valid (the tables allow 0). EPR AVS and the deprecated
     * SPR AVS Sink APDO are not checked.
     */
    bool isValidForReceiver() const;

    /**
     * @brief Print the PDO's kind and the fields that apply to it, one per line
     *
     * The first line is "<indent>Type: <kind>"; each field line is indented two
     * spaces further. Flag lines appear only for a Fixed PDO at position 1, and
     * Peak Current only for a Fixed or SPR AVS PDO sent by a Source.
     *
     * @param s      Output stream
     * @param indent Text printed at the start of every line (may be null)
     */
    void debugPrint(Stream &s, const char *indent = "  ") const;

    // --- Encoders: build a PDO word from its fields --------------------------------
    // Values are converted with integer division by the field's unit and masked to
    // the field width; exact multiples of the unit are lossless.

    /// Fixed PDO, voltage and current only (bits 29:20 left 0)
    static uint32_t encodeFixed(uint16_t voltage_mV, uint16_t current_mA);
    /// Battery PDO (Table 6.11)
    static uint32_t encodeBattery(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t power_mW);
    /// Variable PDO (Table 6.12)
    static uint32_t encodeVariable(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t current_mA);
    /// SPR PPS APDO (Table 6.13 with powerLimited for a Source; pass false for a Sink, Table 6.14)
    static uint32_t encodeSprPps(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t current_mA,
                                 bool powerLimited = false);
    /// SPR AVS Source APDO (Table 6.15)
    static uint32_t encodeSprAvsSource(uint16_t current15V_mA, uint16_t current20V_mA,
                                       PdoPeakCurrent peak = PdoPeakCurrent::IoC);

private:
    uint32_t bits(uint8_t lsb, uint8_t width) const { return (word_ >> lsb) & ((1UL << width) - 1); }
    bool isFixedPdo1() const { return kind() == PdoKind::Fixed && isFirst(); }

    uint32_t word_;
    uint8_t position_;
    PdoRole role_;
};

/**
 * @brief Which rule set validatePdoList() applies
 *
 * Received lists (0x30, 0x31) reject only what USB PD R3.3 tells a receiver to treat
 * as invalid, plus "PDO 1 is the vSafe5V Fixed PDO". Transmit lists (0x32, 0x33) also
 * enforce the sender rules.
 */
enum class PdoListRules : uint8_t {
    ReceivedSource = 0,  ///< RECEIVED_SOURCE_CAPABILITIES (0x30)
    ReceivedSink = 1,    ///< RECEIVED_SINK_CAPABILITIES (0x31)
    TransmitSource = 2,  ///< TRANSMIT_SOURCE_CAPABILITIES (0x32)
    TransmitSink = 3     ///< TRANSMIT_SINK_CAPABILITIES (0x33)
};

/**
 * @brief Validate a Capabilities PDO list against USB PD R3.3
 *
 * Every rule set: each PDO passes TPS25751Pdo::isValidForReceiver(), and PDO 1 is a
 * Fixed PDO at exactly 5 V (100 x 50 mV, Tables 6.8, 6.9; Section 6.4.1.1, page 130).
 *
 * Transmit rule sets also require: the PDO order Fixed, Battery, Variable, SPR AVS,
 * PPS (Section 6.4.1.1, page 130); no EPR AVS or invalid APDO. TransmitSource also
 * requires each Variable PDO's minimum voltage to be at least 80% of its maximum
 * (Table 6.12), and each SPR AVS APDO's 15 V and 20 V currents to equal the Maximum
 * Current of the 15 V and 20 V Fixed PDOs, with the 20 V current 0 when there is no
 * 20 V Fixed PDO (Table 6.15). TransmitSink also rejects an SPR AVS APDO, which a
 * Sink Shall Not send (page 137).
 *
 * @param pdos  The PDOs, pdos[0] being PDO 1
 * @param count Number of PDOs; 0 (no capabilities) is valid
 * @param rules Which rule set to apply
 * @return true if the list passes every rule of the set
 */
bool validatePdoList(const TPS25751Pdo *pdos, uint8_t count, PdoListRules rules);
