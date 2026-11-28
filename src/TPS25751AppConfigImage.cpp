#include "TPS25751AppConfigImage.h"
#include "TPS25751RegisterFactory.h"
#include <string.h>

// No I2C, no clock: this file compiles in the native test build as it is.

namespace {

// Block offsets of the header fields (TPS25751AppConfigImage.h, block layout).
constexpr size_t kVersionByte = 0x00;
constexpr size_t kLengthField = 0x02;
constexpr size_t kCrcField = 0x04;
constexpr size_t kCrcExcludedBytes = 8;  // CRC and customer version word, 0x04 to 0x0B
constexpr size_t kBinLengthLow = 0x15;
constexpr size_t kEveryDeviceCount = 0x17;
constexpr size_t kRecordOffsetField = 0x18;
constexpr size_t kBinLengthHighByte = 0x19;
constexpr uint8_t kHeaderVersion = 1;
constexpr uint16_t kFourBytePortsFlag = 0x8000;
constexpr uint16_t kRecordOffsetMask = 0x03FF;
constexpr uint8_t kBinLengthHighMask = 0x7C;  // block byte 0x19 bits 6:2 = bin length bits 12:8
constexpr uint8_t kLengthMask = 0x3F;
constexpr uint8_t kFlagsShift = 6;

uint16_t readHalf(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void writeHalf(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

uint32_t reverse32(uint32_t v) {
    uint32_t r = 0;
    for (uint8_t i = 0; i < 32; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

}  // namespace

const __FlashStringHelper* toString(TPS25751AppConfigStatus status) {
    switch (status) {
        case TPS25751AppConfigStatus::Ok: return F("ok");
        case TPS25751AppConfigStatus::NullImage: return F("no image, or shorter than its header");
        case TPS25751AppConfigStatus::BadHeaderId: return F("not a patch bundle (Header_ID)");
        case TPS25751AppConfigStatus::BadConfigLocation: return F("app config pointer or size outside the image");
        case TPS25751AppConfigStatus::BadConfigVersion: return F("app config header version is not 1");
        case TPS25751AppConfigStatus::BadConfigLength: return F("app config length out of range");
        case TPS25751AppConfigStatus::UnsupportedLayout: return F("app config layout not supported");
        case TPS25751AppConfigStatus::BadRecords: return F("app config records malformed");
        case TPS25751AppConfigStatus::CrcMismatch: return F("app config CRC mismatch");
        case TPS25751AppConfigStatus::NotFound: return F("no record for that register");
        case TPS25751AppConfigStatus::BadArgument: return F("bad record data or length");
        case TPS25751AppConfigStatus::NoRoom: return F("app config size exceeded");
    }
    return F("unknown");
}

TPS25751AppConfigImage::TPS25751AppConfigImage(uint8_t* image, size_t imageSize)
    : _image(image), _size(imageSize)
{
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

TPS25751AppConfigStatus TPS25751AppConfigImage::checkStructure() const {
    if (_image == nullptr || _size < kMinImageSize) return TPS25751AppConfigStatus::NullImage;
    if (readWord(_image) != kHeaderId) return TPS25751AppConfigStatus::BadHeaderId;

    const size_t offset = configOffset();
    const size_t capacity = configCapacity();
    if (offset < kMinImageSize || capacity < kFirstRecordOffset || offset > _size || capacity > _size - offset) {
        return TPS25751AppConfigStatus::BadConfigLocation;
    }

    const uint8_t* b = block();
    if (b[kVersionByte] != kHeaderVersion) return TPS25751AppConfigStatus::BadConfigVersion;
    const size_t length = configLength();
    if (length < kFirstRecordOffset || length > capacity) return TPS25751AppConfigStatus::BadConfigLength;

    const uint16_t recordField = readHalf(b + kRecordOffsetField);
    if ((recordField & kFourBytePortsFlag) != 0 || b[kEveryDeviceCount] != 0 ||
        (recordField & kRecordOffsetMask) != kFirstRecordOffset) {
        return TPS25751AppConfigStatus::UnsupportedLayout;
    }

    const size_t bin = binTableLength();
    if (bin > length - kFirstRecordOffset) return TPS25751AppConfigStatus::BadRecords;
    const size_t end = recordsEnd();
    size_t at = kFirstRecordOffset;
    while (at < end) {
        if (end - at < kRecordHeaderSize) return TPS25751AppConfigStatus::BadRecords;
        const size_t dataLength = (b[at + 3] & kLengthMask) + 1u;
        if (end - at - kRecordHeaderSize < dataLength) return TPS25751AppConfigStatus::BadRecords;
        at += kRecordHeaderSize + dataLength;
    }
    return TPS25751AppConfigStatus::Ok;
}

TPS25751AppConfigStatus TPS25751AppConfigImage::check() const {
    const TPS25751AppConfigStatus s = checkStructure();
    if (s != TPS25751AppConfigStatus::Ok) return s;
    return storedCrc() == computedCrc() ? TPS25751AppConfigStatus::Ok : TPS25751AppConfigStatus::CrcMismatch;
}

// ---------------------------------------------------------------------------
// Header fields
// ---------------------------------------------------------------------------

size_t TPS25751AppConfigImage::configOffset() const {
    return (_image && _size >= kMinImageSize) ? readWord(_image + kAppConfigPointerOffset) : 0;
}

size_t TPS25751AppConfigImage::configCapacity() const {
    return (_image && _size >= kMinImageSize) ? readWord(_image + kAppConfigSizeOffset) : 0;
}

uint32_t TPS25751AppConfigImage::firmwareVersion() const {
    return (_image && _size >= kMinImageSize) ? readWord(_image + kFirmwareVersionOffset) : 0;
}

uint8_t* TPS25751AppConfigImage::block() const {
    return _image + configOffset();
}

size_t TPS25751AppConfigImage::configLength() const {
    if (checkStructureLocationOnly()) return readHalf(block() + kLengthField);
    return 0;
}

bool TPS25751AppConfigImage::checkStructureLocationOnly() const {
    if (_image == nullptr || _size < kMinImageSize) return false;
    const size_t offset = configOffset();
    const size_t capacity = configCapacity();
    return offset >= kMinImageSize && capacity >= kFirstRecordOffset && offset <= _size && capacity <= _size - offset;
}

uint32_t TPS25751AppConfigImage::storedCrc() const {
    return checkStructureLocationOnly() ? readWord(block() + kCrcField) : 0;
}

uint32_t TPS25751AppConfigImage::computedCrc() const {
    if (!checkStructureLocationOnly()) return 0;
    const size_t length = configLength();
    if (length < kFirstRecordOffset || length > configCapacity()) return 0;
    return appConfigCrc(block(), length);
}

size_t TPS25751AppConfigImage::binTableLength() const {
    const uint8_t* b = block();
    return b[kBinLengthLow] | (static_cast<size_t>((b[kBinLengthHighByte] & kBinLengthHighMask) >> 2) << 8);
}

size_t TPS25751AppConfigImage::recordsEnd() const {
    return configLength() - binTableLength();
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

size_t TPS25751AppConfigImage::recordCount() const {
    size_t n = 0;
    Record r;
    while (record(n, r)) ++n;
    return n;
}

bool TPS25751AppConfigImage::record(size_t index, Record& out) const {
    if (checkStructure() != TPS25751AppConfigStatus::Ok) return false;
    const uint8_t* b = block();
    const size_t end = recordsEnd();
    size_t at = kFirstRecordOffset;
    for (size_t i = 0; at < end; ++i) {
        const uint8_t lengthByte = b[at + 3];
        const uint8_t dataLength = static_cast<uint8_t>((lengthByte & kLengthMask) + 1u);
        if (i == index) {
            out.ports = b[at];
            out.address = b[at + 1];
            out.offset = b[at + 2];
            out.flags = static_cast<uint8_t>(lengthByte >> kFlagsShift);
            out.length = dataLength;
            out.data = b + at + kRecordHeaderSize;
            out.blockOffset = at;
            return true;
        }
        at += kRecordHeaderSize + dataLength;
    }
    return false;
}

bool TPS25751AppConfigImage::findRecord(uint8_t address, Record& out) const {
    Record r;
    for (size_t i = 0; record(i, r); ++i) {
        if (r.address == address) {
            out = r;
            return true;
        }
    }
    return false;
}

std::unique_ptr<TPS25751Register> TPS25751AppConfigImage::recordRegister(TPS25751Registers::Address address) const {
    if (check() != TPS25751AppConfigStatus::Ok) return nullptr;
    Record r;
    if (!findRecord(static_cast<uint8_t>(address), r)) return nullptr;
    const uint8_t size = TPS25751Registers::getRegisterSize(address);
    if (r.offset != 0 || size == 0 || r.length != size) return nullptr;
    TPS25751RegisterFactoryImpl factory;
    return factory.createRegister(address, r.data, r.length);
}

uint8_t TPS25751AppConfigImage::defaultPortMask() const {
    Record first;
    return record(0, first) ? first.ports : kDefaultPortMask;
}

TPS25751AppConfigStatus TPS25751AppConfigImage::setRecord(uint8_t address, const uint8_t* data, uint8_t length) {
    if (data == nullptr || length == 0 || length > kMaxRecordLength) return TPS25751AppConfigStatus::BadArgument;
    const TPS25751AppConfigStatus s = check();
    if (s != TPS25751AppConfigStatus::Ok) return s;

    uint8_t* b = block();
    const size_t oldLength = configLength();
    Record existing;
    const bool replace = findRecord(address, existing);

    size_t at;          // block offset where the new record starts
    size_t oldSpan;     // bytes the old record occupied (0 for an insert)
    uint8_t ports, offset, flags;
    if (replace) {
        at = existing.blockOffset;
        oldSpan = kRecordHeaderSize + existing.length;
        ports = existing.ports;
        offset = existing.offset;
        flags = existing.flags;
    } else {
        at = recordsEnd();
        Record r;
        for (size_t i = 0; record(i, r); ++i) {
            if (r.address > address) {
                at = r.blockOffset;
                break;
            }
        }
        oldSpan = 0;
        ports = defaultPortMask();
        offset = 0;
        flags = 0;
    }

    const size_t newSpan = kRecordHeaderSize + length;
    const size_t newLength = oldLength - oldSpan + newSpan;
    if (newLength > configCapacity() || newLength > 0xFFFF) return TPS25751AppConfigStatus::NoRoom;

    // Move everything after the old record (later records and the bin table), then write.
    memmove(b + at + newSpan, b + at + oldSpan, oldLength - at - oldSpan);
    b[at] = ports;
    b[at + 1] = address;
    b[at + 2] = offset;
    b[at + 3] = static_cast<uint8_t>((flags << kFlagsShift) | (length - 1u));
    memcpy(b + at + kRecordHeaderSize, data, length);
    finishEdit(newLength, oldLength);
    return TPS25751AppConfigStatus::Ok;
}

TPS25751AppConfigStatus TPS25751AppConfigImage::setRecord(TPS25751Registers::Address address, const TPS25751Register& reg) {
    if (!reg.isValid() || reg.size() > kMaxRecordLength) return TPS25751AppConfigStatus::BadArgument;
    return setRecord(static_cast<uint8_t>(address), reg.raw(), static_cast<uint8_t>(reg.size()));
}

TPS25751AppConfigStatus TPS25751AppConfigImage::removeRecord(uint8_t address) {
    const TPS25751AppConfigStatus s = check();
    if (s != TPS25751AppConfigStatus::Ok) return s;
    Record r;
    if (!findRecord(address, r)) return TPS25751AppConfigStatus::NotFound;

    uint8_t* b = block();
    const size_t oldLength = configLength();
    const size_t span = kRecordHeaderSize + r.length;
    memmove(b + r.blockOffset, b + r.blockOffset + span, oldLength - r.blockOffset - span);
    finishEdit(oldLength - span, oldLength);
    return TPS25751AppConfigStatus::Ok;
}

void TPS25751AppConfigImage::finishEdit(size_t newLength, size_t oldLength) {
    uint8_t* b = block();
    // The tool zero-fills the reserved space after the block; keep it that way when the block shrinks.
    if (newLength < oldLength) memset(b + newLength, 0, oldLength - newLength);
    writeHalf(b + kLengthField, static_cast<uint16_t>(newLength));
    writeWord(b + kCrcField, appConfigCrc(b, newLength));
}

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------

uint32_t TPS25751AppConfigImage::crc32(const uint8_t* data, size_t length, uint32_t previous) {
    uint32_t crc = ~previous;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
        }
    }
    return ~crc;
}

uint32_t TPS25751AppConfigImage::storedForm(uint32_t crc32Value) {
    return reverse32(~crc32Value);
}

uint32_t TPS25751AppConfigImage::appConfigCrc(const uint8_t* b, size_t length) {
    if (b == nullptr || length < kCrcField + kCrcExcludedBytes) return 0;
    uint32_t crc = crc32(b, kCrcField);
    crc = crc32(b + kCrcField + kCrcExcludedBytes, length - kCrcField - kCrcExcludedBytes, crc);
    return storedForm(crc);
}

uint32_t TPS25751AppConfigImage::readWord(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void TPS25751AppConfigImage::writeWord(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}
