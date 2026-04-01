#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief Charger detection configuration
 */
enum class ChargerDetectEnable : uint8_t {
    None = 0,           ///< Do not detect any legacy chargers
    BC12Only = 1,       ///< Detect BC 1.2 chargers
    Reserved = 2,       ///< Reserved do not use
    BC12AndProprietary = 3  ///< Detect BC 1.2 and proprietary legacy chargers
};

/**
 * @brief Charger advertisement configuration
 */
enum class ChargerAdvertiseEnable : uint8_t {
    None = 0,           ///< Do not emulate any legacy charger
    CDP = 1,            ///< BC 1.2 CDP only
    DCP = 2,            ///< BC 1.2 DCP only
    Reserved3 = 3,      ///< Reserved
    Reserved4 = 4,      ///< Reserved
    DCP_Auto1 = 5,      ///< DCP Auto 1 (2.7V and DCP)
    DCP_Auto2 = 6,      ///< DCP Auto 2 (1.2V 2.7V and DCP)
    Reserved7 = 7       ///< Reserved
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

    // Charger detection and advertisement
    ChargerDetectEnable chargerDetectEnable() const;
    ChargerAdvertiseEnable chargerAdvertiseEnable() const;
    bool resistor15kPresent() const;

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
