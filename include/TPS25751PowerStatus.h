#ifndef TPS25751POWERSTATUS_H
#define TPS25751POWERSTATUS_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Power Status Register (POWER_STATUS, Address: 0x3F)
 *
 * This register reports detailed status regarding the power of the connection,
 * including charger detection, current capabilities, and connection state.
 *
 * Register size: 2 bytes (16 bits)
 * Access: Read-only
 */
class TPS25751PowerStatus : public TPS25751Register
{
public:
    explicit TPS25751PowerStatus() : TPS25751Register(TPS25751Registers::Registers::POWER_STATUS.size) {};
    explicit TPS25751PowerStatus(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::POWER_STATUS.size) {};
    TPS25751PowerStatus(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751PowerStatus(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    /**
     * @brief Charger advertise status enumeration
     */
    enum class ChargerAdvertiseStatus : uint8_t {
        DISABLED_OR_NOT_RUN = 0,
        IN_PROCESS = 1,
        COMPLETE = 2,
        RESERVED = 3
    };

    /**
     * @brief Charger detection status enumeration
     */
    enum class ChargerDetectStatus : uint8_t {
        DISABLED_OR_NOT_RUN = 0,
        IN_PROGRESS = 1,
        COMPLETE_NONE = 2,
        COMPLETE_SDP = 3,          // Standard Downstream Port
        COMPLETE_BC12_CDP = 4,     // Battery Charging 1.2 Charging Downstream Port
        COMPLETE_BC12_DCP = 5,     // Battery Charging 1.2 Dedicated Charging Port
        COMPLETE_DIVIDER1_DCP = 6,
        COMPLETE_DIVIDER2_DCP = 7,
        COMPLETE_DIVIDER3_DCP = 8,
        COMPLETE_1_2V_DCP = 9
    };

    /**
     * @brief Type-C current capability enumeration
     */
    enum class TypeCCurrent : uint8_t {
        USB_DEFAULT = 0,           // USB Default Current
        CURRENT_1_5A = 1,          // 1.5 A
        CURRENT_3_0A = 2,          // 3.0 A
        EXPLICIT_PD_CONTRACT = 3   // Explicit PD contract sets current
    };

    // Charger status (bits 9:8, 7:4)
    ChargerAdvertiseStatus getChargerAdvertiseStatus() const;
    ChargerDetectStatus getChargerDetectStatus() const;

    // Type-C current capability (bits 3:2)
    TypeCCurrent getTypeCCurrent() const;

    // Source/Sink and connection status (bits 1:0)
    bool isSourceSink() const;          // bit 1: 0=requests power, 1=provides power
    bool isPowerConnection() const;      // bit 0: connection present

    /**
     * @brief Check if device is providing power (acting as sink)
     * @return true if PD controller is providing power
     */
    bool isProvidingPower() const { return isSourceSink(); }

    /**
     * @brief Check if device is requesting power (acting as source)
     * @return true if connection is requesting power
     */
    bool isRequestingPower() const { return !isSourceSink(); }

    /**
     * @brief Get human-readable charger advertise status string
     */
    const char* getChargerAdvertiseStatusString() const;

    /**
     * @brief Get human-readable charger detect status string
     */
    const char* getChargerDetectStatusString() const;

    /**
     * @brief Get human-readable Type-C current string
     */
    const char* getTypeCCurrentString() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates power status register data semantically
     * @return true if power status data is logically consistent
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751POWERSTATUS_H