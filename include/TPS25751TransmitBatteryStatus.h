#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751TransmitBatteryStatus.h
 * @brief TRANSMITTED_BATTERY_STATUS_DATA_OBJECTS_(BSDO) register (0x7B) class implementation
 *
 * This register holds the four Battery Status Data Objects (BSDOs) that the
 * controller sends in reply to a USB PD 3.x Get_Battery_Status message, one entry
 * per battery index 0 to 3.
 *
 * Every field is "as defined in the USB PD specification" (Battery Status Data
 * Object). The TRM does not restate the encodings, so the raw accessors return the
 * field bits unchanged. The typed accessors beside them decode each entry per USB
 * PD R3.3 Table 6.24 (Battery Status Data Object): the Invalid Battery Reference
 * and Battery Present flags, the Battery Charging Status, and the Battery Present
 * Capacity in 0.1 Wh steps (reported here in mWh). Each entry is one BSDO, so
 * BSDO bit 8 is bit 0 of the entry's Battery Info byte.
 *
 * Gate bit: PD3_CONFIGURATION (0x42) bit 11, TPS25751PD3Config::supportBatteryStatus().
 * The TRM calls it SupportBatteryStatusMsg. With the bit clear, the controller
 * ignores this register and answers Get_Battery_Status with Not_Supported. With
 * it set, the controller sends the contents of this register. The controller
 * takes no automatic action when this register is written.
 *
 * The TRM says the host should also program the battery counts in
 * TX_SOURCE_CAPABILITIES_EXTENDED (0x77) so that they are consistent with the
 * entries populated here (SLVUCR8B Rev. B, p.57). One register cannot check
 * that agreement, so semantic validation does not.
 *
 * Register Details:
 * - Address: 0x7B
 * - Size: 16 bytes (128 bits); the controller's length byte is 16 on hardware
 * - Access: Read/Write
 * - Reset: 000000000000000000000000FFFF0200h (Battery 0 Battery Info 2h, Battery 0
 *   Present Capacity FFFFh). The reset contains 0xFF bytes but is not all 0xFF, so
 *   the data tier accepts it. A TPS25751 sinking from a PD charger reads exactly
 *   this value.
 * - Reserved bits: 103:96, 71:64, 39:32, and 7:0 (the low byte of each entry;
 *   validated to read 0)
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 16-byte buffer. Multi-byte fields
 * are little-endian. The register is four 4-byte entries, entry i at byte 4 * i
 * (bit 32 * i):
 *   byte 4i        reserved (bits 32i+7:32i)
 *   byte 4i+1      Battery i Battery Info (bits 32i+15:32i+8)
 *   bytes 4i+2..3  Battery i Present Capacity (bits 32i+31:32i+16)
 *
 * Byte map:
 *   byte 0       reserved (7:0)
 *   byte 1       Battery 0 Battery Info (15:8)
 *   bytes 2-3    Battery 0 Present Capacity (31:16)
 *   byte 4       reserved (39:32)
 *   byte 5       Battery 1 Battery Info (47:40)
 *   bytes 6-7    Battery 1 Present Capacity (63:48)
 *   byte 8       reserved (71:64)
 *   byte 9       Battery 2 Battery Info (79:72)
 *   bytes 10-11  Battery 2 Present Capacity (95:80)
 *   byte 12      reserved (103:96)
 *   byte 13      Battery 3 Battery Info (111:104)
 *   bytes 14-15  Battery 3 Present Capacity (127:112)
 *
 * Naming: the TRM names battery 3's 16-bit field "Battery 3 Present Info" while
 * batteries 0 to 2 use "Present Capacity". Table 3-36 of the PDF has the same
 * text, so this is a TRM naming error. The library uses one name for all four
 * entries, rawPresentCapacity(3).
 *
 * The highest bit position is 112, so extractBits*() and setBits() reach every
 * field of this register.
 *
 * Hardware reading (2026-11-19, board sinking from a PD charger): the length
 * byte is 16 and the register reads 00 02 FF FF followed by twelve zero bytes,
 * the documented reset. Writes round-tripped exactly and the firmware did not
 * overwrite them. On that board PD3_CONFIGURATION bit 11 was 0, so the controller
 * was not answering Get_Battery_Status from this register.
 *
 * @code
 * TPS25751 tps;
 * TPS25751TransmitBatteryStatus status;
 * if (tps.readTransmitBatteryStatus(status)) {
 *     status.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.57
 * @see TPS25751PD3Config
 * @see TPS25751TransmitSourceCapsExtended
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751TransmitBatteryStatus : public TPS25751Register
{
public:
    explicit TPS25751TransmitBatteryStatus() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_BATTERY_STATUS.size) {}
    explicit TPS25751TransmitBatteryStatus(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_BATTERY_STATUS.size) {}
    TPS25751TransmitBatteryStatus(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitBatteryStatus(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_BATTERY_STATUS;

    /// Number of battery entries (indexes 0 to 3).
    static constexpr uint8_t kBatteryCount = 4;
    /// Size of one entry in bytes.
    static constexpr uint8_t kEntrySize = 4;

    // --- Decode (raw values; encodings are defined by the USB PD specification) ---

    /**
     * @brief Battery Info of one entry (raw)
     * @param battery Battery index 0 to 3: bits 15:8, 47:40, 79:72, and 111:104
     * @return The raw byte, or 0 if @p battery is 4 or more or the register is invalid
     */
    uint8_t batteryInfo(uint8_t battery) const;

    /**
     * @brief Present Capacity of one entry (raw, little-endian)
     *
     * TI names battery 3's field "Battery 3 Present Info" in the TRM; it is the
     * same field as for the other entries.
     *
     * @param battery Battery index 0 to 3: bits 31:16, 63:48, 95:80, and 127:112
     * @return The raw 16-bit value, or 0 if @p battery is 4 or more or the register is invalid
     */
    uint16_t rawPresentCapacity(uint8_t battery) const;

    // --- Typed decode, USB PD R3.3 Table 6.24 (built on the raw accessors above) ---
    // Every accessor returns false or 0 for a battery index of 4 or more.

    /// @brief Battery Charging Status, BSDO bits 11:10 (Battery Info bits 3:2; USB PD R3.3 Table 6.24)
    enum class ChargingStatus : uint8_t {
        Charging = 0,     ///< 00b - Battery is charging
        Discharging = 1,  ///< 01b - Battery is discharging
        Idle = 2,         ///< 10b - Battery is idle
        Invalid = 3       ///< 11b - Invalid; the receiver ignores the field
    };

    /// @brief Text for a ChargingStatus; any code above 3 is "Undefined"
    static const char *toString(ChargingStatus val);

    /**
     * @brief Invalid Battery Reference, BSDO bit 8 (Battery Info bit 0; USB PD R3.3 Table 6.24)
     *
     * True when the Get_Battery_Status message named a battery or slot that does
     * not exist.
     */
    bool invalidBatteryReference(uint8_t battery) const;

    /// @brief Battery Present, BSDO bit 9 (Battery Info bit 1; USB PD R3.3 Table 6.24)
    bool batteryPresent(uint8_t battery) const;

    /**
     * @brief Battery Charging Status, BSDO bits 11:10 (Battery Info bits 3:2; USB PD R3.3 Table 6.24)
     *
     * Meaningful only when batteryPresent() is true and presentCapacityKnown() is
     * true; otherwise the specification marks the field reserved. Code 3 passes
     * through as ChargingStatus::Invalid. Returns Charging (0) for an index of 4 or more.
     */
    ChargingStatus chargingStatus(uint8_t battery) const;

    /// @brief True when Battery Present Capacity is not FFFFh, which means unknown (USB PD R3.3 Table 6.24)
    bool presentCapacityKnown(uint8_t battery) const;

    /**
     * @brief Battery state of charge in milliwatt-hours (USB PD R3.3 Table 6.24)
     *
     * The raw field counts 0.1 Wh steps (100 mWh) from 0000h to FFFEh, so the
     * result is at most 6,553,400 mWh. Returns 0 when the capacity is unknown
     * (FFFFh) or the index is out of range.
     */
    uint32_t presentCapacity_mWh(uint8_t battery) const;

    // --- Setters: each a single setBits() call after the index guard ---
    // No setter touches the reserved bytes 0, 4, 8, and 12.

    /**
     * @brief Set Battery Info of one entry
     * @param battery Battery index 0 to 3; a larger index changes nothing
     * @param value Raw byte for bits 32 * battery + 15:32 * battery + 8
     */
    void setBatteryInfo(uint8_t battery, uint8_t value);

    /**
     * @brief Set Present Capacity of one entry
     * @param battery Battery index 0 to 3; a larger index changes nothing
     * @param value Raw 16-bit value for bits 32 * battery + 31:32 * battery + 16
     */
    void setRawPresentCapacity(uint8_t battery, uint16_t value);

    /**
     * @brief Print the 16 raw bytes and every field
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: reserved bytes 0, 4, 8, and 12 (bits 7:0, 39:32, 71:64, and
     * 103:96) read 0. There is no other rule: the TRM gives no encoding for
     * any field, and the agreement of the entries with the battery counts in
     * register 0x77 cannot be checked from this register alone.
     */
    bool isSemanticallyValid() const override;

private:
    /// First bit of battery @p battery's Battery Info field: entry start (kEntrySize * 8 * battery) + 8.
    /// The caller range-checks @p battery; the result is at most 104.
    static uint8_t batteryInfoBit(uint8_t battery) { return static_cast<uint8_t>(kEntrySize * 8 * battery + 8); }
    /// First bit of battery @p battery's Present Capacity field: entry start + 16 (at most 112).
    static uint8_t presentCapacityBit(uint8_t battery) { return static_cast<uint8_t>(kEntrySize * 8 * battery + 16); }
};
