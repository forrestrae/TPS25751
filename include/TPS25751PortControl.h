#pragma once


#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief Charger detection configuration (PORT_CONTROL bits 31:30)
 *
 * Source: SLVUCR8B (Rev. B, 2026-08), page 31. Rev. B defines 2h as
 * "Detect proprietary chargers only"; Rev. A marked it "Reserved do not
 * use". Reserved is a deprecated alias for ProprietaryOnly with the same value.
 */
enum class ChargerDetectEnable : uint8_t {
    None = 0,           ///< Do not detect any legacy chargers
    BC12Only = 1,       ///< Detect BC 1.2 chargers
    ProprietaryOnly = 2,    ///< Detect proprietary chargers only (Rev. B; Reserved in Rev. A)
    BC12AndProprietary = 3, ///< Detect BC 1.2 and proprietary legacy chargers

    /// @deprecated Rev. A name for 2h. Same value as ProprietaryOnly.
    Reserved [[deprecated("SLVUCR8B Rev. B p.31 defines 2h as proprietary-only detection; use ChargerDetectEnable::ProprietaryOnly")]] = 2
};

/**
 * @brief Charger advertisement configuration (PORT_CONTROL bits 28:26)
 *
 * Source: SLVUCR8B (Rev. B, 2026-08), page 31. Rev. B describes 0h as
 * "BC 1.2 SDP only"; Rev. A said "Do not emulate any legacy charger".
 * None is a deprecated alias for SDP with the same value.
 */
enum class ChargerAdvertiseEnable : uint8_t {
    SDP = 0,            ///< BC 1.2 SDP only (Rev. B; "no legacy charger emulation" in Rev. A)
    CDP = 1,            ///< BC 1.2 CDP only
    DCP = 2,            ///< BC 1.2 DCP only
    Reserved3 = 3,      ///< Reserved
    Reserved4 = 4,      ///< Reserved
    DCP_Auto1 = 5,      ///< DCP Auto 1 (2.7V and DCP)
    DCP_Auto2 = 6,      ///< DCP Auto 2 (1.2V 2.7V and DCP)
    Reserved7 = 7,      ///< Reserved

    /// @deprecated Rev. A name for 0h. Same value as SDP.
    None [[deprecated("SLVUCR8B Rev. B p.31 defines 0h as BC 1.2 SDP only; use ChargerAdvertiseEnable::SDP")]] = 0
};

/**
 * @brief Type-C current advertisement
 */
enum class TypeCCurrent : uint8_t {
    USBDefault = 0,     ///< USB Default Current
    Current_1_5A = 1,   ///< 1.5 A
    Current_3_0A = 2,   ///< 3.0 A
    Reserved = 3        ///< Reserved
};

/**
 * @brief TPS25751 Port Control Register (0x29)
 *
 * Configuration bits affecting system policy. These bits may change during normal
 * operation and are used for controlling the respective port. The PD Controller will
 * not take immediate action upon writing. Changes made to this register will take
 * effect the next time the appropriate policy is invoked.
 *
 * Register Size: 4 bytes
 * Access: Read/Write
 * Address: 0x29
 */
class TPS25751PortControl : public TPS25751Register
{
public:
    explicit TPS25751PortControl() : TPS25751Register(TPS25751Registers::Registers::PORT_CONTROL.size) {}
    explicit TPS25751PortControl(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::PORT_CONTROL.size) {}
    TPS25751PortControl(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751PortControl(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::PORT_CONTROL;

    // Charger detection and advertisement
    ChargerDetectEnable chargerDetectEnable() const;
    ChargerAdvertiseEnable chargerAdvertiseEnable() const;
    bool resistor15kPresent() const;

    /**
     * @brief DCD Enable (bit 25)
     *
     * Set to enable Data Contact Detect as defined by BC 1.2 for sinks.
     * SLVUCR8B (Rev. B, 2026-08), page 31; reset 0h. New in Rev. B.
     */
    bool dcdEnable() const;

    /**
     * @brief Automatic ID Request (bit 16)
     *
     * When set, the controller automatically issues a Discover Identity VDM
     * for all SOP types when appropriate. SLVUCR8B (Rev. B, 2026-08), page 31;
     * reset 1h, so a freshly reset register reads true. New in Rev. B.
     */
    bool automaticIdRequest() const;

    // Current monitoring
    bool enableCurrentMonitor() const;

    // Power configuration
    bool unconstrainedPower() const;

    // Data Role (DR) Swap configuration
    bool initiateSwapToDFP() const;
    bool processSwapToDFP() const;
    bool initiateSwapToUFP() const;
    bool processSwapToUFP() const;

    // Power Role (PR) Swap configuration
    bool initiateSwapToSource() const;
    bool processSwapToSource() const;
    bool initiateSwapToSink() const;
    bool processSwapToSink() const;

    // Type-C current
    TypeCCurrent typeCCurrent() const;

    // --- Setters: each a single setBits() call, touching only its own field ----
    // Bit positions: SLVUCR8B (Rev. B, 2026-08), page 31.

    /// @brief Set Charger Detect Enable (bits 31:30)
    void setChargerDetectEnable(ChargerDetectEnable value);
    /// @brief Set Charger Advertise Enable (bits 28:26)
    void setChargerAdvertiseEnable(ChargerAdvertiseEnable value);
    /// @brief Set Resistor 15k Present (bit 24)
    void setResistor15kPresent(bool present);
    /// @brief Set DCD Enable (bit 25)
    void setDcdEnable(bool enable);
    /// @brief Set Automatic ID Request (bit 16)
    void setAutomaticIdRequest(bool enable);
    /// @brief Set Enable Current Monitor (bit 20)
    void setEnableCurrentMonitor(bool enable);
    /// @brief Set Unconstrained Power (bit 19)
    void setUnconstrainedPower(bool enable);
    /// @brief Set Initiate Swap to DFP (bit 15)
    void setInitiateSwapToDFP(bool enable);
    /// @brief Set Process Swap to DFP (bit 14)
    void setProcessSwapToDFP(bool enable);
    /// @brief Set Initiate Swap to UFP (bit 13)
    void setInitiateSwapToUFP(bool enable);
    /// @brief Set Process Swap to UFP (bit 12)
    void setProcessSwapToUFP(bool enable);
    /// @brief Set Initiate Swap to Source (bit 7)
    void setInitiateSwapToSource(bool enable);
    /// @brief Set Process Swap to Source (bit 6)
    void setProcessSwapToSource(bool enable);
    /// @brief Set Initiate Swap to Sink (bit 5)
    void setInitiateSwapToSink(bool enable);
    /// @brief Set Process Swap to Sink (bit 4)
    void setProcessSwapToSink(bool enable);
    /// @brief Set TypeC Current (bits 1:0)
    void setTypeCCurrent(TypeCCurrent value);

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates port control register data semantically
     * @return true if port control data is logically consistent
     */
    bool isSemanticallyValid() const override;
};

// String conversion helpers
const char* toString(ChargerDetectEnable val);
const char* toString(ChargerAdvertiseEnable val);
const char* toString(TypeCCurrent val);
