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

uint8_t TPS25751Register::extractBits(uint8_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        TPS_DEBUG_TRACE(DEBUG_CAT_PARSING, "extractBits: invalid register (pos=%u, len=%u)", bitPos, bitLen);
        return 0;
    }
    uint8_t result = extractBits(_raw, _len, bitPos, bitLen);
    TPS_DEBUG_TRACE(DEBUG_CAT_PARSING, "extractBits: pos=%u, len=%u -> 0x%02X", bitPos, bitLen, result);
    return result;
}

uint16_t TPS25751Register::extractBits16(uint8_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        return 0;
    }
    return extractBits16(_raw, _len, bitPos, bitLen);
}

uint32_t TPS25751Register::extractBits32(uint8_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        return 0;
    }
    return extractBits32(_raw, _len, bitPos, bitLen);
}

uint64_t TPS25751Register::extractBits64(uint8_t bitPos, uint8_t bitLen) const
{
    if (!isValid())
    {
        return 0;
    }
    return extractBits64(_raw, _len, bitPos, bitLen);
}

// Bounds-checked static methods
uint8_t TPS25751Register::extractBits(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 8 || dataSize == 0)
    {
        return 0;
    }

    uint8_t byteStart = bitPos / 8;
    uint8_t bitOffset = bitPos % 8;
    uint8_t bytesNeeded = (bitOffset + bitLen + 7) / 8;

    // Check bounds
    if (byteStart >= dataSize || (byteStart + bytesNeeded) > dataSize)
    {
        return 0;
    }

    uint16_t value = (data[byteStart] >> bitOffset);
    if (bitOffset + bitLen > 8 && (static_cast<size_t>(byteStart) + 1) < dataSize)
    {
        value |= (data[byteStart + 1] << (8 - bitOffset));
    }
    return value & ((1 << bitLen) - 1);
}

uint16_t TPS25751Register::extractBits16(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 16 || dataSize == 0)
    {
        return 0;
    }

    uint8_t byteStart = bitPos / 8;
    uint8_t bitOffset = bitPos % 8;
    uint8_t bytesNeeded = (bitOffset + bitLen + 7) / 8;

    // Check bounds
    if (byteStart >= dataSize || (byteStart + bytesNeeded) > dataSize)
    {
        return 0;
    }

    uint32_t value = 0;
    // Extract only the bytes we know are valid
    for (uint8_t i = 0; i < bytesNeeded && (byteStart + i) < dataSize && i < 3; ++i)
    {
        value |= (static_cast<uint32_t>(data[byteStart + i]) << (i * 8));
    }

    value >>= bitOffset;
    return value & ((1UL << bitLen) - 1);
}

uint32_t TPS25751Register::extractBits32(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen)
{
    if (!data || bitLen == 0 || bitLen > 32 || dataSize == 0)
    {
        return 0;
    }

    uint8_t byteStart = bitPos / 8;
    uint8_t bitOffset = bitPos % 8;
    uint8_t bytesNeeded = (bitOffset + bitLen + 7) / 8;

    // Check bounds
    if (byteStart >= dataSize || (byteStart + bytesNeeded) > dataSize)
    {
        return 0;
    }

    uint64_t value = 0;
    // Extract only the bytes we know are valid
    for (uint8_t i = 0; i < bytesNeeded && (byteStart + i) < dataSize && i < 5; ++i)
    {
        value |= (static_cast<uint64_t>(data[byteStart + i]) << (i * 8));
    }

    value >>= bitOffset;
    return value & ((1ULL << bitLen) - 1);
}

uint64_t TPS25751Register::extractBits64(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen)
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

    // For true 64-bit fields, ensure both halves are within bounds
    uint64_t lowBits = extractBits32(data, dataSize, bitPos, 32);
    uint64_t highBits = extractBits32(data, dataSize, bitPos + 32, bitLen - 32);

    return lowBits | (highBits << 32);
}

size_t TPS25751Register::extractArray32(uint16_t startBitPos, uint8_t elementBitLen, size_t numElements, uint32_t *output) const
{
    if (!isValid() || !output || elementBitLen == 0 || elementBitLen > 32)
    {
        return 0;
    }

    size_t extractedCount = 0;
    uint16_t currentBitPos = startBitPos;

    for (size_t i = 0; i < numElements; ++i)
    {
        // Check if we have enough bits remaining in the register
        size_t bitsRemaining = (_len * 8) - currentBitPos;
        if (bitsRemaining < elementBitLen)
        {
            break; // Not enough data for this element
        }

        // Use bounds-checked extraction
        output[i] = extractBits32(_raw, _len, static_cast<uint8_t>(currentBitPos), elementBitLen);
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
