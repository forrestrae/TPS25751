#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751TransmitBatteryCapabilities.h
 * @brief TX_BATTERY_CAPABILITIES register (0x7D) class implementation
 *
 * This register holds the four Battery Capability Data Blocks (BCDBs) that the
 * controller sends in reply to a USB PD 3.x Get_Battery_Capabilities message, one entry
 * per battery index 0 to 3. Each entry carries the battery's VID, PID, design
 * capacity, last full charge capacity, and battery type.
 *
 * Every field is "as defined in the USB PD specification" (Battery_Capabilities
 * message). The TRM does not restate the encodings and the register-docs server
 * does not serve the USB PD specification, so this class exposes raw values only:
 * no units, scale factors, enums, or bit meanings. Typed decode is planned for
 * Batch L.
 *
 * Gate bit: PD3_CONFIGURATION (0x42) bit 10, TPS25751PD3Config::supportBatteryCapabilities().
 * The TRM calls it SupportBatteryCapMsg. With the bit clear, the controller ignores
 * this register and answers Get_Battery_Capabilities with Not_Supported. With it
 * set, the controller sends the contents of this register. The controller takes no
 * automatic action when this register is written.
 *
 * The TRM says the host should also program the battery counts in
 * TX_SOURCE_CAPABILITIES_EXTENDED (0x77) so that they are consistent with the
 * entries populated here (SLVUCR8B Rev. B, p.58). One register cannot check that
 * agreement, so semantic validation does not.
 *
 * Register Details:
 * - Address: 0x7D
 * - Size: 36 bytes (288 bits); the controller's length byte is 36 on hardware
 * - Access: Read/Write
 * - Reset: all zero
 * - Reserved bits: none
 *
 * Bit n lives in byte n / 8, bit n % 8 of the 36-byte buffer. Multi-byte fields
 * are little-endian. The register is four 9-byte entries, entry i at byte 9 * i
 * (bit 72 * i). Within an entry every field is byte-aligned:
 *   offset 0-1  VID
 *   offset 2-3  PID
 *   offset 4-5  Battery Design Capacity
 *   offset 6-7  Battery Last Full Charge Capacity
 *   offset 8    Battery Type
 *
 * Byte map (bytes and bits):
 *   Battery 0   VID 0-1 (15:0)     PID 2-3 (31:16)    Design 4-5 (47:32)     Last Full 6-7 (63:48)     Type 8 (71:64)
 *   Battery 1   VID 9-10 (87:72)   PID 11-12 (103:88) Design 13-14 (119:104) Last Full 15-16 (135:120) Type 17 (143:136)
 *   Battery 2   VID 18-19 (159:144) PID 20-21 (175:160) Design 22-23 (191:176) Last Full 24-25 (207:192) Type 26 (215:208)
 *   Battery 3   VID 27-28 (231:216) PID 29-30 (247:232) Design 31-32 (263:248) Last Full 33-34 (279:264) Type 35 (287:280)
 *
 * TRM labeling error: the TRM describes the five fields at bits 287:216 as
 * "battery index 0". They are named VID 3 and PID 3 and sit at the fourth 72-bit
 * stride, so the only reading consistent with the names and the layout is that
 * they belong to battery index 3. Table 3-37 of the PDF has the same text. The
 * library treats bits 287:216 as battery 3. Three of the field names (Battery
 * Type, Battery Last Full Charge Capacity, Battery Design Capacity) repeat four
 * times in the TRM, so this class identifies every field by its bit range, never
 * by name.
 *
 * Why byte assembly: this is the library's first R/W register with fields past
 * bit 255. extractBits*() and setBits() take a uint8_t bit position, which cannot
 * address battery 3 (its Design Capacity starts at bit 248, its Last Full Charge
 * Capacity at bit 264, and its Battery Type at bit 280); a cast would wrap to a low
 * bit and corrupt battery 0. The class therefore never calls extractBits*() or
 * setBits(). Every field of every battery goes through class-private
 * bounds-checked little-endian byte helpers: reads follow the technique of
 * TPS25751IOConfig::readGpioBitmap() and writes are the matching inverse, so one
 * decode path serves all four entries.
 *
 * Hardware reading (2026-11-19, board sinking from a PD charger): the length
 * byte is 36 and the register read all zero, the documented reset. Writes
 * round-tripped exactly and restored exactly, and the firmware did not overwrite
 * them. A write that changed only bytes 27 to 35 left bytes 0 to 26 unchanged.
 * On that board PD3_CONFIGURATION bit 10 was 0, so the controller was not
 * answering Get_Battery_Capabilities from this register.
 *
 * @code
 * TPS25751 tps;
 * TPS25751TransmitBatteryCapabilities caps;
 * if (tps.readTransmitBatteryCapabilities(caps)) {
 *     caps.debugPrint();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.58
 * @see TPS25751PD3Config
 * @see TPS25751TransmitSourceCapsExtended
 * @see TPS25751IOConfig
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751TransmitBatteryCapabilities : public TPS25751Register
{
public:
    explicit TPS25751TransmitBatteryCapabilities() : TPS25751Register(TPS25751Registers::Registers::TRANSMIT_BATTERY_CAPABILITIES.size) {}
    explicit TPS25751TransmitBatteryCapabilities(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::TRANSMIT_BATTERY_CAPABILITIES.size) {}
    TPS25751TransmitBatteryCapabilities(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751TransmitBatteryCapabilities(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::TRANSMIT_BATTERY_CAPABILITIES;

    /// Number of battery entries (indexes 0 to 3).
    static constexpr uint8_t kBatteryCount = 4;
    /// Size of one entry in bytes.
    static constexpr uint8_t kEntrySize = 9;

    // Byte offsets of each field within an entry.
    static constexpr uint8_t kVidOffset = 0;                        ///< VID, 2 bytes
    static constexpr uint8_t kPidOffset = 2;                        ///< PID, 2 bytes
    static constexpr uint8_t kDesignCapacityOffset = 4;             ///< Battery Design Capacity, 2 bytes
    static constexpr uint8_t kLastFullChargeCapacityOffset = 6;     ///< Battery Last Full Charge Capacity, 2 bytes
    static constexpr uint8_t kBatteryTypeOffset = 8;                ///< Battery Type, 1 byte

    // --- Decode (raw values; encodings are defined by the USB PD specification) ---
    // Each accessor returns 0 for a battery index of 4 or more, for an invalid
    // register, and for a field the buffer is too short to hold.

    /**
     * @brief VID of one entry (raw, little-endian)
     * @param battery Battery index 0 to 3: bytes 0-1 (15:0), 9-10 (87:72), 18-19 (159:144), 27-28 (231:216)
     */
    uint16_t vid(uint8_t battery) const;

    /**
     * @brief PID of one entry (raw, little-endian)
     * @param battery Battery index 0 to 3: bytes 2-3 (31:16), 11-12 (103:88), 20-21 (175:160), 29-30 (247:232)
     */
    uint16_t pid(uint8_t battery) const;

    /**
     * @brief Battery Design Capacity of one entry (raw, little-endian)
     * @param battery Battery index 0 to 3: bytes 4-5 (47:32), 13-14 (119:104), 22-23 (191:176), 31-32 (263:248)
     */
    uint16_t batteryDesignCapacity(uint8_t battery) const;

    /**
     * @brief Battery Last Full Charge Capacity of one entry (raw, little-endian)
     * @param battery Battery index 0 to 3: bytes 6-7 (63:48), 15-16 (135:120), 24-25 (207:192), 33-34 (279:264)
     */
    uint16_t batteryLastFullChargeCapacity(uint8_t battery) const;

    /**
     * @brief Battery Type of one entry (raw)
     * @param battery Battery index 0 to 3: byte 8 (71:64), 17 (143:136), 26 (215:208), 35 (287:280)
     */
    uint8_t batteryType(uint8_t battery) const;

    // --- Setters: each writes only its own field's bytes and is a no-op for battery >= 4 ---

    /// @brief Set VID of one entry (bytes 9 * battery + 0 and + 1)
    void setVid(uint8_t battery, uint16_t value);
    /// @brief Set PID of one entry (bytes 9 * battery + 2 and + 3)
    void setPid(uint8_t battery, uint16_t value);
    /// @brief Set Battery Design Capacity of one entry (bytes 9 * battery + 4 and + 5)
    void setBatteryDesignCapacity(uint8_t battery, uint16_t value);
    /// @brief Set Battery Last Full Charge Capacity of one entry (bytes 9 * battery + 6 and + 7)
    void setBatteryLastFullChargeCapacity(uint8_t battery, uint16_t value);
    /// @brief Set Battery Type of one entry (byte 9 * battery + 8)
    void setBatteryType(uint8_t battery, uint8_t value);

    /**
     * @brief Print the 36 raw bytes and every field
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks only (valid buffer, not the all-0xFF bus-error signature,
     * expected size). The register has no reserved bits and the TRM states no
     * rule for any field, so there is nothing further to check.
     */
    bool isSemanticallyValid() const override;

private:
    /// Little-endian 16-bit read at @p byteOffset; 0 unless the register is valid and both bytes are in range.
    uint16_t readLE16(size_t byteOffset) const;
    /// Byte read at @p byteOffset; 0 unless the register is valid and the byte is in range.
    uint8_t readByte(size_t byteOffset) const;
    /// Little-endian 16-bit write at @p byteOffset; no-op unless the register is valid and both bytes are in range.
    void writeLE16(size_t byteOffset, uint16_t value);
    /// Byte write at @p byteOffset; no-op unless the register is valid and the byte is in range.
    void writeByte(size_t byteOffset, uint8_t value);
    /// First byte of battery @p battery's entry (the caller range-checks @p battery).
    static size_t entryOffset(uint8_t battery) { return static_cast<size_t>(kEntrySize) * battery; }
};
