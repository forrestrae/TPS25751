#include "TPS25751Register.h"

TPS25751Register::TPS25751Register() : _raw(nullptr), _len(0)
{
}

TPS25751Register::TPS25751Register(const size_t len) : _raw(nullptr), _len(len)
{
    if (len > 0)
    {
        _raw = new uint8_t[_len]();
    }
}

TPS25751Register::TPS25751Register(const uint8_t *data, const size_t len) : TPS25751Register(len)
{
    if (data && _raw && _len > 0)
    {
        memcpy(_raw, data, _len);
    }
}

// Copy constructor
TPS25751Register::TPS25751Register(const TPS25751Register &other) : _raw(nullptr), _len(other._len)
{
    if (other._raw && _len > 0)
    {
        _raw = new uint8_t[_len]();
        memcpy(_raw, other._raw, _len);
    }
}

// Copy assignment operator
TPS25751Register &TPS25751Register::operator=(const TPS25751Register &other)
{
    if (this != &other)
    {
        delete[] _raw;
        _raw = nullptr;
        _len = other._len;

        if (other._raw && _len > 0)
        {
            _raw = new uint8_t[_len]();
            memcpy(_raw, other._raw, _len);
        }
    }
    return *this;
}

// Move constructor
TPS25751Register::TPS25751Register(TPS25751Register &&other) noexcept
    : _raw(other._raw), _len(other._len)
{
    other._raw = nullptr;
    other._len = 0;
}

// Move assignment operator
TPS25751Register &TPS25751Register::operator=(TPS25751Register &&other) noexcept
{
    if (this != &other)
    {
        delete[] _raw;
        _raw = other._raw;
        _len = other._len;

        other._raw = nullptr;
        other._len = 0;
    }
    return *this;
}

TPS25751Register::~TPS25751Register()
{
    delete[] _raw;
}

uint8_t TPS25751Register::extractBits(uint16_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        TPS_DEBUG_TRACE(DEBUG_CAT_PARSING, "extractBits: invalid register (pos=%u, len=%u)", bitPos, bitLen);
        return 0;
    }
    if (bitLen > 8)
    {
        // extractBits() is the 8-bit extractor and returns 0 for wider fields;
        // a caller asking for more than 8 bits wants extractBits16/32/64.
        // Warn so this programming error is visible; the return value is unchanged.
        TPS_DEBUG_WARN(DEBUG_CAT_PARSING, "extractBits: len=%u > 8 at pos=%u returns 0; use extractBits16/32/64", bitLen, bitPos);
    }
    uint8_t result = extractBits(_raw, _len, bitPos, bitLen);
    TPS_DEBUG_TRACE(DEBUG_CAT_PARSING, "extractBits: pos=%u, len=%u -> 0x%02X", bitPos, bitLen, result);
    return result;
}

uint16_t TPS25751Register::extractBits16(uint16_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        return 0;
    }
    return extractBits16(_raw, _len, bitPos, bitLen);
}

uint32_t TPS25751Register::extractBits32(uint16_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        return 0;
    }
    return extractBits32(_raw, _len, bitPos, bitLen);
}

uint64_t TPS25751Register::extractBits64(uint16_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        return 0;
    }
    return extractBits64(_raw, _len, bitPos, bitLen);
}

void TPS25751Register::setBits(uint16_t bitPos, uint8_t bitLen, uint32_t value)
{
    if (!isValid())
    {
        TPS_DEBUG_TRACE(DEBUG_CAT_PARSING, "setBits: invalid register (pos=%u, len=%u)", bitPos, bitLen);
        return;
    }
    setBits(_raw, _len, bitPos, bitLen, value);
    TPS_DEBUG_TRACE(DEBUG_CAT_PARSING, "setBits: pos=%u, len=%u <- 0x%08lX", bitPos, bitLen, (unsigned long)value);
}

// Bounds-checked static methods
//
// Every byte computation below is done in size_t. bitPos is 16 bits, so a field
// can start anywhere in a buffer of up to 8 KB, and no sum of start byte and
// bytes needed can wrap before it is compared with dataSize.
namespace {

// Locates a field of bitLen bits at bitPos in a buffer of dataSize bytes.
// Returns false when any byte of the field lies outside the buffer.
bool locateField(size_t dataSize, uint16_t bitPos, uint8_t bitLen,
                 size_t &byteStart, uint8_t &bitOffset, size_t &bytesNeeded)
{
    byteStart = static_cast<size_t>(bitPos) / 8;
    bitOffset = static_cast<uint8_t>(bitPos % 8);
    bytesNeeded = (static_cast<size_t>(bitOffset) + bitLen + 7) / 8;
    return byteStart < dataSize && bytesNeeded <= dataSize - byteStart;
}

}  // namespace

uint8_t TPS25751Register::extractBits(const uint8_t *data, size_t dataSize, uint16_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 8 || dataSize == 0)
    {
        return 0;
    }

    size_t byteStart;
    uint8_t bitOffset;
    size_t bytesNeeded;
    if (!locateField(dataSize, bitPos, bitLen, byteStart, bitOffset, bytesNeeded))
    {
        return 0;
    }

    uint16_t value = (data[byteStart] >> bitOffset);
    if (bytesNeeded > 1)
    {
        value |= (data[byteStart + 1] << (8 - bitOffset));
    }
    return value & ((1 << bitLen) - 1);
}

uint16_t TPS25751Register::extractBits16(const uint8_t *data, size_t dataSize, uint16_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 16 || dataSize == 0)
    {
        return 0;
    }

    size_t byteStart;
    uint8_t bitOffset;
    size_t bytesNeeded;
    if (!locateField(dataSize, bitPos, bitLen, byteStart, bitOffset, bytesNeeded))
    {
        return 0;
    }

    uint32_t value = 0;
    // Extract only the bytes we know are valid
    for (size_t i = 0; i < bytesNeeded; ++i)
    {
        value |= (static_cast<uint32_t>(data[byteStart + i]) << (i * 8));
    }

    value >>= bitOffset;
    return value & ((1UL << bitLen) - 1);
}

uint32_t TPS25751Register::extractBits32(const uint8_t *data, size_t dataSize, uint16_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 32 || dataSize == 0)
    {
        return 0;
    }

    size_t byteStart;
    uint8_t bitOffset;
    size_t bytesNeeded;
    if (!locateField(dataSize, bitPos, bitLen, byteStart, bitOffset, bytesNeeded))
    {
        return 0;
    }

    uint64_t value = 0;
    // Extract only the bytes we know are valid
    for (size_t i = 0; i < bytesNeeded; ++i)
    {
        value |= (static_cast<uint64_t>(data[byteStart + i]) << (i * 8));
    }

    value >>= bitOffset;
    return value & ((1ULL << bitLen) - 1);
}

uint64_t TPS25751Register::extractBits64(const uint8_t *data, size_t dataSize, uint16_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 64 || dataSize == 0)
    {
        return 0;
    }

    // For fields > 32 bits, use split approach with bounds checking
    if (bitLen <= 32)
    {
        return extractBits32(data, dataSize, bitPos, bitLen);
    }

    // For true 64-bit fields, each 32-bit half is bounds-checked on its own, so
    // a field whose upper half runs off the buffer returns its lower half (the
    // CustomerUse short-buffer test relies on this). The upper half's position
    // is computed in 32 bits; one that does not fit in 16 bits is past any
    // buffer, so that half reads as 0 instead of wrapping to a low bit.
    const uint32_t highPos = static_cast<uint32_t>(bitPos) + 32u;
    uint64_t lowBits = extractBits32(data, dataSize, bitPos, 32);
    uint64_t highBits = (highPos > 0xFFFFu)
                            ? 0
                            : extractBits32(data, dataSize, static_cast<uint16_t>(highPos), bitLen - 32);

    return lowBits | (highBits << 32);
}

void TPS25751Register::setBits(uint8_t *data, size_t dataSize, uint16_t bitPos, uint8_t bitLen, uint32_t value)
{
    if (!data || bitLen == 0 || bitLen > 32 || dataSize == 0)
    {
        return;
    }

    size_t byteStart;
    uint8_t bitOffset;
    size_t bytesNeeded;
    if (!locateField(dataSize, bitPos, bitLen, byteStart, bitOffset, bytesNeeded))
    {
        return;
    }

    // Mask the value to the field width so an over-wide value cannot bleed into
    // neighbouring fields. bitLen is 1..32 here, so (1<<bitLen) needs 64-bit math.
    const uint64_t widthMask = (bitLen >= 32) ? 0xFFFFFFFFULL : ((1ULL << bitLen) - 1);
    const uint64_t maskedValue = static_cast<uint64_t>(value) & widthMask;

    // Assemble the affected bytes little-endian (matching extractBits assembly),
    // read-modify-write the field, then write the bytes back. bitOffset (<=7) +
    // bitLen (<=32) spans at most 5 bytes, well within the 64-bit window.
    uint64_t window = 0;
    for (size_t i = 0; i < bytesNeeded; ++i)
    {
        window |= static_cast<uint64_t>(data[byteStart + i]) << (i * 8);
    }

    const uint64_t fieldMask = widthMask << bitOffset;
    window = (window & ~fieldMask) | (maskedValue << bitOffset);

    for (size_t i = 0; i < bytesNeeded; ++i)
    {
        data[byteStart + i] = static_cast<uint8_t>((window >> (i * 8)) & 0xFF);
    }
}

size_t TPS25751Register::extractArray32(uint16_t startBitPos, uint8_t elementBitLen, size_t numElements, uint32_t *output) const
{
    if (!isValid() || !output || elementBitLen == 0 || elementBitLen > 32)
    {
        return 0;
    }

    size_t extractedCount = 0;
    const size_t totalBits = _len * 8;
    size_t currentBitPos = startBitPos;

    for (size_t i = 0; i < numElements; ++i)
    {
        // Stop when the element does not fit in the register, or its position
        // does not fit in the 16-bit bitPos of extractBits32()
        if (currentBitPos > totalBits || totalBits - currentBitPos < elementBitLen ||
            currentBitPos > 0xFFFFu)
        {
            break;
        }

        // Use bounds-checked extraction
        output[i] = extractBits32(_raw, _len, static_cast<uint16_t>(currentBitPos), elementBitLen);
        currentBitPos += elementBitLen;
        extractedCount++;
    }

    return extractedCount;
}

bool TPS25751Register::hasValidData() const
{
    if (!isValid())
    {
        TPS_DEBUG_TRACE(DEBUG_CAT_VALIDATION, "hasValidData: register invalid (null ptr or zero length)");
        return false;
    }

    // Check if all bytes are zero (uninitialized)
    // bool allZeros = true; // Some registers such as Status can contain all zeros, so disabling this check.
    bool allOnes = true;

    for (size_t i = 0; i < _len; i++)
    {
        // if (_raw[i] != 0)
        //     allZeros = false;
        if (_raw[i] != 0xFF)
            allOnes = false;
        // if (!allZeros && !allOnes)
        if (!allOnes)
            break; // Early exit if we know it's mixed data
    }

    // Invalid if all zeros or all ones (likely uninitialized or error state)
    // bool hasValid = !allZeros && !allOnes;
    bool hasValid = !allOnes;
    if (!hasValid)
    {
        // const char *reason = allZeros ? "all zeros" : "all ones";
        const char *reason = "all ones";
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "hasValidData: register data appears invalid (%s, len=%u)", reason, (unsigned)_len);
        TPS_DEBUG_HEXDUMP(DEBUG_CAT_VALIDATION, _raw, _len, "Invalid data");
    }

    return hasValid;
}

void TPS25751Register::debugDumpRaw(Stream &s, const char *prefix) const
{
    if (!isValid())
    {
        s.println(F("Register invalid - cannot dump"));
        return;
    }

    if (prefix)
    {
        s.print(prefix);
        s.print(F(" ("));
        s.print(_len);
        s.print(F(" bytes)"));
    }
    else
    {
        s.print(F("Register dump ("));
        s.print(_len);
        s.print(F(" bytes)"));
    }
    s.println(F(":"));

    TPS25751Debug::hexDump(s, _raw, _len, nullptr);
}

void TPS25751Register::printHex(Stream &s, uint32_t value, uint8_t digits)
{
    if (digits > 8)
    {
        digits = 8;
    }
    for (uint8_t i = digits; i > 0; --i)
    {
        s.print(static_cast<unsigned int>((value >> ((i - 1) * 4)) & 0xFu), HEX);
    }
}
