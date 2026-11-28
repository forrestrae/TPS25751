#pragma once

/**
 * @file TPS25751AppConfigImage.h
 * @brief Reads and edits the application configuration block of a patch bundle
 *
 * A low-region binary from TI's USBCPD Application Customization Tool (a patch
 * bundle) holds the firmware patch and an application configuration block: the
 * register values the controller loads at boot. This class finds that block in
 * a caller-owned image, lists its records, hands a record out as a register
 * class, replaces, adds, or removes records, and keeps the block's length and
 * CRC correct, so the image can be loaded with TPS25751PatchLoader or written
 * with TPS25751EepromUpdater.
 *
 * Nothing here talks to the controller. The class is not a register and is not
 * in the factory (ADR-016).
 *
 * Image layout (the tool's LowRegionService; every multi-byte field is
 * little-endian):
 *
 * | Image offset | Field |
 * |---|---|
 * | 0x00 | Header_ID 0xACE00001 |
 * | 0x08, 0x0C, 0x10 | Code patch pointer, size, and CRC |
 * | 0x34 | Short firmware version (the VERSION register, 0x0F) |
 * | 0x44 | App config size: the bytes reserved for the block |
 * | 0x48 | App config pointer: the block's image offset (0x80) |
 *
 * Block layout (the tool's AppConfigService.generateAppConfig()):
 *
 * | Block offset | Field |
 * |---|---|
 * | 0x00 | Header version, 1 |
 * | 0x02 | Block length in bytes, header and bin table included |
 * | 0x04 | CRC, see appConfigCrc() |
 * | 0x08 | Customer version, then three reserved bytes |
 * | 0x0C | USB-IF XID |
 * | 0x10 | Host interface unlock code, four ASCII characters |
 * | 0x14 | Protection flags |
 * | 0x15 | Bin table length, bits 7:0 |
 * | 0x16 | Bin table record count |
 * | 0x17 | Every-device record count; the tool writes 0 |
 * | 0x18 | Bits 9:0 first record offset (0x1A); bit 15 set when a record's port field is four bytes; bits 14:10 bin table length, bits 12:8 |
 * | 0x1A | Records, then the bin table, which ends the block |
 *
 * A record is `{ports, register, offset, (flags << 6) | (length - 1), data...}`:
 * a port mask (0x0F on the board's image), the register address, the byte
 * offset in the register where the data starts, two flag bits the tool sets to
 * 0, and 1 to 64 data bytes. The data bytes are the register's bytes as an I2C
 * read returns them after the length byte, so a record whose offset is 0 and
 * whose length is the register's size decodes with the register class.
 *
 * Records the tool would not write: it drops a record whose data is all zero,
 * so the controller keeps that register's firmware default. setRecord() writes
 * what it is given, zeros included.
 *
 * The bin table is opaque to this class and is moved, never changed.
 */

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/// Outcome of a TPS25751AppConfigImage check or edit.
enum class TPS25751AppConfigStatus : uint8_t {
    Ok,                 ///< The image is consistent; an edit was applied
    NullImage,          ///< No buffer, or the image is shorter than its header (0x4C bytes)
    BadHeaderId,        ///< The image does not start with Header_ID 0xACE00001
    BadConfigLocation,  ///< The app config pointer and size do not fit in the image
    BadConfigVersion,   ///< The block's header version is not 1
    BadConfigLength,    ///< The block length is shorter than its header or longer than the app config size
    UnsupportedLayout,  ///< Four-byte port fields, or every-device records, which the tool does not write
    BadRecords,         ///< A record runs past the bin table, or the bin table past the block
    CrcMismatch,        ///< The stored CRC is not the CRC of the block
    NotFound,           ///< removeRecord(): no record for that register
    BadArgument,        ///< setRecord(): no data, or a length outside 1 to 64
    NoRoom,             ///< setRecord(): the edited block would not fit in the app config size
};

/// Text for a TPS25751AppConfigStatus, in flash.
const __FlashStringHelper* toString(TPS25751AppConfigStatus status);

class TPS25751AppConfigImage {
public:
    /// Header_ID at image offset 0 (SLVAFL1 section 2.1).
    static constexpr uint32_t kHeaderId = 0xACE00001UL;
    /// Image offsets of the header fields this class reads.
    static constexpr size_t kCodePatchPointerOffset = 0x08;
    static constexpr size_t kCodePatchSizeOffset = 0x0C;
    static constexpr size_t kCodePatchCrcOffset = 0x10;
    static constexpr size_t kFirmwareVersionOffset = 0x34;
    static constexpr size_t kAppConfigSizeOffset = 0x44;
    static constexpr size_t kAppConfigPointerOffset = 0x48;
    /// Shortest image the class accepts: the header through the app config pointer.
    static constexpr size_t kMinImageSize = 0x4C;
    /// Block offset of the first record, and the size of the block header.
    static constexpr size_t kFirstRecordOffset = 0x1A;
    /// Bytes in a record ahead of its data.
    static constexpr size_t kRecordHeaderSize = 4;
    /// Largest record data length: the length field is 6 bits of length - 1.
    static constexpr uint8_t kMaxRecordLength = 64;
    /// Port mask setRecord() gives a new record when the block has none to copy.
    static constexpr uint8_t kDefaultPortMask = 0x0F;

    /// One record of the block. data points into the image and is valid until the next edit.
    struct Record {
        uint8_t ports = 0;        ///< Port mask
        uint8_t address = 0;      ///< Register address
        uint8_t offset = 0;       ///< First register byte the data covers
        uint8_t flags = 0;        ///< Bits 7:6 of the length byte
        uint8_t length = 0;       ///< Data bytes, 1 to 64
        const uint8_t* data = nullptr;
        size_t blockOffset = 0;   ///< Block offset of the record's first byte
    };

    /**
     * @brief Wraps an image the caller owns; nothing is copied.
     * @param image     A low-region image, or one region of a full-flash image
     * @param imageSize Bytes in image
     */
    TPS25751AppConfigImage(uint8_t* image, size_t imageSize);

    /// Checks the header, the block, every record, and the CRC. Every edit requires Ok first.
    TPS25751AppConfigStatus check() const;

    /// Image offset of the block (the app config pointer). Valid when check() is not NullImage.
    size_t configOffset() const;
    /// Bytes reserved for the block (the app config size).
    size_t configCapacity() const;
    /// Bytes the block uses (block offset 0x02).
    size_t configLength() const;
    /// CRC stored at block offset 0x04, as a little-endian word.
    uint32_t storedCrc() const;
    /// CRC of the block as it is now; equals storedCrc() in a consistent image.
    uint32_t computedCrc() const;
    /// Short firmware version at image offset 0x34.
    uint32_t firmwareVersion() const;

    /// Number of records. 0 unless check() passes the structure checks.
    size_t recordCount() const;
    /// The record at index (0 based, in block order). False past the last one.
    bool record(size_t index, Record& out) const;
    /// The first record for a register. False if there is none.
    bool findRecord(uint8_t address, Record& out) const;

    /**
     * @brief The record for a register as its register class
     *
     * Null when check() is not Ok, when there is no record, when the record's
     * offset is not 0, or when its length is not the register's size.
     */
    std::unique_ptr<TPS25751Register> recordRegister(TPS25751Registers::Address address) const;

    /// recordRegister() cast to the class for address. T must be that register's class.
    template<typename T>
    std::unique_ptr<T> recordAs(TPS25751Registers::Address address) const {
        // The factory creates the class for address, so static_cast is safe; null casts to null.
        return std::unique_ptr<T>(static_cast<T*>(recordRegister(address).release()));
    }

    /**
     * @brief Replaces a register's record, or adds one, then updates the length and CRC
     *
     * An existing record keeps its port mask, offset, and flags, and takes the
     * new data and length. A new record is put before the first record with a
     * higher address, with offset 0, flags 0, and the port mask of the first
     * record (kDefaultPortMask when there is none). The bin table moves with
     * the records. Nothing changes unless the result is Ok.
     */
    TPS25751AppConfigStatus setRecord(uint8_t address, const uint8_t* data, uint8_t length);

    /// setRecord() with a register's whole buffer.
    TPS25751AppConfigStatus setRecord(TPS25751Registers::Address address, const TPS25751Register& reg);

    /// setRecord() for a read/write register class, which names its own address.
    template<typename T>
    TPS25751AppConfigStatus setRecord(const T& reg) {
        return setRecord(T::kAddress, static_cast<const TPS25751Register&>(reg));
    }

    /// Removes a register's record, then updates the length and CRC. The controller keeps that register's firmware default.
    TPS25751AppConfigStatus removeRecord(uint8_t address);

    /// Standard CRC-32 (reflected polynomial 0xEDB88320, initial and final XOR 0xFFFFFFFF); chain with previous.
    static uint32_t crc32(const uint8_t* data, size_t length, uint32_t previous = 0);

    /**
     * @brief The tool's CRC of an app config block, as stored at block offset 0x04
     *
     * The tool takes the CRC-32 of the block without bytes 0x04 to 0x0B (the CRC
     * and the customer version word), writes it big-endian, and stores each
     * byte inverted and bit-reversed. As a little-endian word that is the
     * bit-reversed complement of the CRC-32. The customer version is therefore
     * not covered. The code patch CRC at image offset 0x10 uses the same
     * transform over the code patch.
     */
    static uint32_t appConfigCrc(const uint8_t* block, size_t length);

    /// The stored form of a CRC-32: complemented and bit-reversed.
    static uint32_t storedForm(uint32_t crc32Value);

private:
    uint8_t* _image;
    size_t _size;

    TPS25751AppConfigStatus checkStructure() const;
    bool checkStructureLocationOnly() const;
    uint8_t* block() const;
    size_t binTableLength() const;
    size_t recordsEnd() const;
    uint8_t defaultPortMask() const;
    void finishEdit(size_t newLength, size_t oldLength);

    static uint32_t readWord(const uint8_t* p);
    static void writeWord(uint8_t* p, uint32_t value);
};
