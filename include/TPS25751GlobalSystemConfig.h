#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751GlobalSystemConfig.h
 * @brief GLOBAL_SYSTEM_CONFIGURATION register (0x27) class implementation
 *
 * GLOBAL_SYSTEM_CONFIGURATION holds the configuration that is common to all
 * ports: the RCP threshold on PP_EXT, the PP3 switch role, the PP_5V current
 * limit overshoot, the PP1 (PP_5V) role, and whether PP_CABLE1 supplies VCONN.
 * It is initialized by Application Customization and rarely changes at run time.
 *
 * TRM text: "Any modifications to this register will cause a port disconnect and
 * reconnect with the new settings."
 *
 * PP1 and PP3 describe how the board's power switches are wired. Writing a PP3
 * role that does not match the hardware can back-drive the power path, so write
 * this register only with values taken from a read of the same board.
 *
 * The firmware stores undocumented values in bits the TRM lists as reserved
 * (hardware read 2026-11-18, see isSemanticallyValid()). A write must therefore
 * start from a fresh read of the register (read-modify-write). Writing a
 * default-constructed object would clear those bits as well as rewrite every
 * field.
 *
 * Register Details:
 * - Address: 0x27
 * - Size: 14 bytes. The TRM gives 116 bits (15 bytes), but the controller
 *   reports a 14-byte length on hardware (2026-11-18), so bits 115:112 are not
 *   transmitted. The library reads and writes 14 bytes.
 * - Access: Read/Write
 * - Reset: 00000000000000000000000028101h
 * - Reserved bits: 115:24, 21:19, 13:11, 7:1 (not validated; the firmware sets them)
 *
 * Bit n lives in byte n / 8, bit n % 8, so the reset value is the buffer
 * {0x01, 0x81, 0x02, 0x00 x 11}.
 *
 * @code
 * TPS25751 tps;
 * TPS25751GlobalSystemConfig gsc;
 * if (tps.readGlobalSystemConfig(gsc)) {
 *     gsc.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.28
 * @see TPS25751Register
 * @see TPS25751
 */

/// RCP threshold on PP_EXT (bits 23:22)
enum class RcpThreshold : uint8_t {
    mV6 = 0,   ///< 0h - 6 mV (nominal)
    mV8 = 1,   ///< 1h - 8 mV (nominal)
    mV10 = 2,  ///< 2h - 10 mV (nominal)
    mV12 = 3   ///< 3h - 12 mV (nominal)
};

/// PP3 switch configuration (bits 18:16). Codes 6 and 7 are undefined in the TRM.
enum class Pp3Config : uint8_t {
    Disabled = 0,               ///< 0h - PP3 not used and disabled
    Source = 1,                 ///< 1h - PP3 is a Source (output)
    Sink = 2,                   ///< 2h - PP3 is a Sink (input)
    SinkWaitSrdy = 3,           ///< 3h - PP3 is sink but waits for 'SRDY'
    Bidirectional = 4,          ///< 4h - PP3 is bi-directional
    BidirectionalWaitSrdy = 5   ///< 5h - PP3 is bi-directional but waits for 'SRDY'
};

/**
 * PP_5V current-limit overshoot (bits 15:14): the margin the firmware adds when
 * it selects the current limit for PP5V to VBUS. This is not the APDO ILIM Over
 * Shoot field in PORT_CONFIGURATION (0x28).
 */
enum class IlimOvershoot : uint8_t {
    None = 0,    ///< 0h - No additional overshoot margin
    mA100 = 1,   ///< 1h - Overshoot margin of at least 100 mA
    mA200 = 2,   ///< 2h - Overshoot margin of at least 200 mA
    mA500 = 3    ///< 3h - Overshoot margin of at least 500 mA
};

/// PP1 (PP_5V) configuration (bits 10:8). Codes 2 to 7 are undefined in the TRM.
enum class Pp1Config : uint8_t {
    Disabled = 0,  ///< 0h - Not used (disabled)
    Source = 1     ///< 1h - PP1 configured as source
};

class TPS25751GlobalSystemConfig : public TPS25751Register
{
public:
    explicit TPS25751GlobalSystemConfig() : TPS25751Register(TPS25751Registers::Registers::GLOBAL_SYSTEM_CONFIGURATION.size) {}
    explicit TPS25751GlobalSystemConfig(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::GLOBAL_SYSTEM_CONFIGURATION.size) {}
    TPS25751GlobalSystemConfig(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751GlobalSystemConfig(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::GLOBAL_SYSTEM_CONFIGURATION;

    // --- Decode ---------------------------------------------------------------

    /// @brief RCP Threshold on PP_EXT (bits 23:22)
    RcpThreshold rcpThreshold() const;

    /**
     * @brief PP3 Config (bits 18:16)
     *
     * The cast of the raw 3-bit code, so the undefined codes 6 and 7 come back
     * as values outside the enumerators. Use rawPp3Config() to read the code.
     */
    Pp3Config pp3Config() const;

    /// @brief Raw 3-bit PP3 Config code (bits 18:16)
    uint8_t rawPp3Config() const;

    /// @brief ILIM Over Shoot for PP_5V to VBUS (bits 15:14); see IlimOvershoot
    IlimOvershoot ilimOvershoot() const;

    /**
     * @brief PP1 Config (bits 10:8)
     *
     * The cast of the raw 3-bit code, so the undefined codes 2 to 7 come back as
     * values outside the enumerators. Use rawPp1Config() to read the code.
     */
    Pp1Config pp1Config() const;

    /// @brief Raw 3-bit PP1 Config code (bits 10:8)
    uint8_t rawPp1Config() const;

    /**
     * @brief PP Cable1 Switch Config (bit 0)
     *
     * When set, the controller enables VCONN on PP_CABLE1 when the USB
     * specification requires it. Set at reset.
     */
    bool ppCable1SwitchEnabled() const;

    // --- Setters: each a single setBits() call, touching only its own field ----

    /// @brief Set RCP Threshold (bits 23:22)
    void setRcpThreshold(RcpThreshold value);
    /// @brief Set PP3 Config (bits 18:16)
    void setPp3Config(Pp3Config value);
    /// @brief Set ILIM Over Shoot (bits 15:14)
    void setIlimOvershoot(IlimOvershoot value);
    /// @brief Set PP1 Config (bits 10:8)
    void setPp1Config(Pp1Config value);
    /// @brief Set PP Cable1 Switch Config (bit 0)
    void setPpCable1SwitchEnabled(bool enabled);

    /**
     * @brief Print each field with its text and raw code, 
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: the raw PP3 code is at most 5 and the raw PP1 code is at most 1.
     * Reserved bits are not checked, because the firmware sets them.
     */
    bool isSemanticallyValid() const override;

    // --- Bit positions and masks ----------------------------------------------

    static constexpr uint8_t kRcpThresholdBit = 22;
    static constexpr uint8_t kPp3ConfigBit = 16;
    static constexpr uint8_t kIlimOvershootBit = 14;
    static constexpr uint8_t kPp1ConfigBit = 8;
    static constexpr uint8_t kPpCable1Bit = 0;

    /// Highest defined PP3 Config code (BidirectionalWaitSrdy)
    static constexpr uint8_t kMaxPp3Code = 5;
    /// Highest defined PP1 Config code (Source)
    static constexpr uint8_t kMaxPp1Code = 1;
};

/// @brief TRM text for an RCP threshold value
const char *toString(RcpThreshold val);
/// @brief TRM text for a PP3 Config value ("Undefined" for codes 6 and 7)
const char *toString(Pp3Config val);
/// @brief TRM text for an ILIM Over Shoot value
const char *toString(IlimOvershoot val);
/// @brief TRM text for a PP1 Config value ("Undefined" for codes 2 to 7)
const char *toString(Pp1Config val);
