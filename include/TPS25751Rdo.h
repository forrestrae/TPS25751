#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "TPS25751Pdo.h"

/**
 * @file TPS25751Rdo.h
 * @brief USB PD Request Data Object (RDO) value type for ACTIVE_RDO_CONTRACT (0x35)
 *
 * An RDO's layout depends on the PDO it requests, the one at its Object Position in
 * the Source_Capabilities (USB PD R3.3 V1.0, Section 6.4.2.1.5, page 144). The word
 * alone does not say which, so the caller supplies the kind, usually through
 * rdoKindFor() applied to that PDO:
 *
 * | RdoKind        | Table | Bits 19:10 or 20:9                 | Bits 9:0 or 6:0                       |
 * |----------------|-------|------------------------------------|---------------------------------------|
 * | FixedVariable  | 6.19  | Operating Current, 10 mA (19:10)   | Maximum Operating Current (deprecated copy, 9:0) |
 * | Battery        | 6.20  | Operating Power, 250 mW (19:10)    | Maximum Operating Power (deprecated copy, 9:0)   |
 * | Pps            | 6.21  | Output Voltage, 20 mV (20:9)       | Operating Current, 50 mA (6:0)        |
 * | Avs            | 6.22  | Output Voltage, 25 mV (20:9; bits 10:9 Shall be 00b, so 100 mV steps) | Operating Current, 50 mA (6:0) |
 *
 * Bits 31:22 are common to every kind (Tables 6.19 to 6.22, page 142): Object
 * Position, Giveback (deprecated), Capability Mismatch, USB Communications Capable,
 * No USB Suspend, Unchunked Extended Messages Supported, and EPR Capable (bit 22,
 * which the TRM marks reserved in 0x35). Accessors that do not apply to the kind
 * return 0, the same convention as TPS25751Pdo.
 */

/// @brief Which RDO layout a word follows (Tables 6.19 to 6.22)
enum class RdoKind : uint8_t {
    FixedVariable = 0,  ///< Fixed and Variable RDO (Table 6.19)
    Battery = 1,        ///< Battery RDO (Table 6.20)
    Pps = 2,            ///< Programmable Power Supply RDO (Table 6.21)
    Avs = 3,            ///< Adjustable Voltage Supply RDO (Table 6.22)
    Unknown = 4         ///< Not known; only the common fields decode
};

/// @brief Text for an RdoKind
const char *toString(RdoKind kind);

/**
 * @brief The RDO layout that requests a given PDO
 *
 * Fixed and Variable PDOs give FixedVariable, Battery gives Battery, the SPR PPS
 * APDO gives Pps, and both AVS APDOs give Avs (Section 6.4.2.1.5, page 144). An
 * invalid APDO gives Unknown.
 */
RdoKind rdoKindFor(const TPS25751Pdo &pdo);

/// @brief rdoKindFor() for a bare PDO word (role and position do not affect the kind)
RdoKind rdoKindFor(uint32_t pdoWord);

/**
 * @brief One USB PD RDO word, decoded as the layout of its kind
 */
class TPS25751Rdo {
public:
    TPS25751Rdo() : word_(0), kind_(RdoKind::Unknown) {}
    TPS25751Rdo(uint32_t word, RdoKind kind) : word_(word), kind_(kind) {}

    uint32_t raw() const { return word_; }  ///< The RDO word as read
    RdoKind kind() const { return kind_; }  ///< The layout used to decode it

    // --- Common fields, every kind (Tables 6.19 to 6.22) ----------------------------

    uint8_t objectPosition() const;          ///< Bits 31:28: 1-based position of the requested PDO
    bool giveback() const;                   ///< Bit 27: deprecated, receiver Shall ignore
    bool capabilityMismatch() const;         ///< Bit 26
    bool usbCommunicationsCapable() const;   ///< Bit 25 (the tables print "USB Communications Cable")
    bool noUsbSuspend() const;               ///< Bit 24
    bool unchunkedExtendedMessages() const;  ///< Bit 23
    bool eprCapable() const;                 ///< Bit 22

    // --- FixedVariable (Table 6.19) -------------------------------------------------

    /// Bits 19:10 (FixedVariable or Battery): Operating Current (10 mA) or Operating Power (250 mW); 0 otherwise
    uint16_t rawOperating() const;
    /// Bits 9:0 (FixedVariable or Battery): the deprecated Maximum Operating Current or Power; 0 otherwise
    uint16_t rawMaximumOperating() const;

    /**
     * @brief Operating current in mA
     *
     * FixedVariable: bits 19:10 x 10 mA. Pps and Avs: bits 6:0 x 50 mA. Battery and
     * Unknown: 0 (a Battery RDO carries power instead).
     */
    uint16_t operatingCurrent_mA() const;

    /// FixedVariable: deprecated Maximum Operating Current, bits 9:0 x 10 mA; 0 otherwise
    uint16_t maximumOperatingCurrent_mA() const;

    // --- Battery (Table 6.20) -------------------------------------------------------

    uint32_t operatingPower_mW() const;         ///< Battery: bits 19:10 x 250 mW; 0 otherwise
    uint32_t maximumOperatingPower_mW() const;  ///< Battery: deprecated copy, bits 9:0 x 250 mW; 0 otherwise

    // --- Pps and Avs (Tables 6.21, 6.22) --------------------------------------------

    uint16_t rawOutputVoltage() const;  ///< Bits 20:9 (Pps or Avs); 0 otherwise
    /// Output voltage in mV: Pps x 20 mV, Avs x 25 mV (100 mV effective steps); 0 otherwise
    uint32_t outputVoltage_mV() const;
    uint8_t rawPpsAvsOperatingCurrent() const;  ///< Bits 6:0 (Pps or Avs), 50 mA units; 0 otherwise

    /**
     * @brief True when the deprecated maximum field equals the operating field
     *
     * Tables 6.19 and 6.20 say the transmitter Shall set the deprecated Maximum
     * Operating Current (or Power) equal to the Operating Current (or Power). Always
     * true for Pps, Avs, and Unknown, which have no such copy.
     */
    bool deprecatedCopyMatches() const;

    /**
     * @brief Print the RDO's common fields and the fields of its kind, one per line
     * @param s      Output stream
     * @param indent Text printed at the start of every line (may be null)
     */
    void debugPrint(Stream &s, const char *indent = "  ") const;

private:
    uint32_t bits(uint8_t lsb, uint8_t width) const { return (word_ >> lsb) & ((1UL << width) - 1); }

    uint32_t word_;
    RdoKind kind_;
};
