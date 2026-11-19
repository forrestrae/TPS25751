#pragma once

#include <stdint.h>

/**
 * @file TPS25751UsbPd.h
 * @brief USB PD specification definitions shared by more than one register class
 *
 * TX_SOURCE_CAPABILITIES_EXTENDED (0x77) and TRANSMIT_SINK_CAPABILITIES_EXTENDED
 * (0x7E) carry the same two USB PD R3.3 encodings: the 2-bit load step rate
 * (Source_Capabilities_Extended Voltage Regulation bits 1:0, Table 6.59;
 * Sink_Capabilities_Extended Load Step bits 1:0, Table 6.70) and the 16-bit
 * overload word (Source_Capabilities_Extended Peak Current 1 to 3, Table 6.59;
 * Sink_Capabilities_Extended Sink Load Characteristics, Table 6.70). Both classes
 * decode them here, so the two cannot drift apart.
 *
 * Overload word layout (both tables):
 *   bits 4:0   percent overload in 10% steps, 0 to 25; 26 to 31 are invalid and
 *              treated as 25
 *   bits 10:5  overload period in 20 ms steps
 *   bits 14:11 duty cycle in 5% steps (the specification text says 0 to 31, but
 *              the field is 4 bits wide, so the largest value is 15, 75%)
 *   bit 15     VBUS may droop an additional 5% during overload
 */
namespace TPS25751UsbPd {

/// @brief Load step rate, 2-bit field (USB PD R3.3 Tables 6.59 and 6.70); codes 2 and 3 are invalid and the receiver assumes the default
enum class LoadStep : uint8_t {
    Rate150mAPerUs = 0,  ///< 00b - 150 mA/us load step (default)
    Rate500mAPerUs = 1   ///< 01b - 500 mA/us load step
};

/// @brief Text for a LoadStep; codes 2 and 3 are "Invalid (assume 150 mA/us)"
inline const char *toString(LoadStep val)
{
    switch (val) {
        case LoadStep::Rate150mAPerUs: return "150 mA/us";
        case LoadStep::Rate500mAPerUs: return "500 mA/us";
        default: return "Invalid (assume 150 mA/us)";
    }
}

/// @brief Load step rate from the low two bits of @p raw (other bits ignored); invalid codes pass through cast into the enum
inline LoadStep loadStepFromRaw(uint8_t raw)
{
    return static_cast<LoadStep>(raw & 0x03u);
}

/// @brief Overload word bits 4:0 in percent: 10% steps, 26 to 31 treated as 25, so 0 to 250
inline uint16_t overloadPercent(uint16_t word)
{
    uint16_t steps = word & 0x1Fu;  // bits 4:0
    if (steps > 25) steps = 25;     // 26..31 invalid, treated as 25
    return static_cast<uint16_t>(steps * 10);
}

/// @brief Overload word bits 10:5 in milliseconds: 20 ms steps, 0 to 1260
inline uint16_t overloadPeriod_ms(uint16_t word)
{
    return static_cast<uint16_t>(((word >> 5) & 0x3Fu) * 20);
}

/// @brief Overload word bits 14:11 in percent: 5% steps, 0 to 75
inline uint8_t overloadDutyCyclePercent(uint16_t word)
{
    return static_cast<uint8_t>(((word >> 11) & 0x0Fu) * 5);
}

/// @brief Overload word bit 15: true when VBUS may droop an additional 5% during overload
inline bool overloadVbusDroop(uint16_t word)
{
    return (word & 0x8000u) != 0;
}

}  // namespace TPS25751UsbPd
