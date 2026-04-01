#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include "TPS25751Debug.h"

class TPS25751Register {
public:
    explicit TPS25751Register();
    explicit TPS25751Register(size_t len);
    TPS25751Register(const uint8_t *data, size_t len);

    // Rule of Five implementation
    TPS25751Register(const TPS25751Register& other);
    TPS25751Register& operator=(const TPS25751Register& other);
    TPS25751Register(TPS25751Register&& other) noexcept;
    TPS25751Register& operator=(TPS25751Register&& other) noexcept;

    virtual ~TPS25751Register();
    virtual void debugPrint(Stream &s = Serial) const = 0;

protected:
    uint8_t *_raw;
    size_t _len;

#ifdef PIO_UNIT_TESTING
public:  // Make protected methods public for unit testing
#endif

public:
    /**
     * @brief Checks if the register data is valid for access
     * @return true if _raw is not null and _len > 0
     */
    bool isValid() const { return _raw != nullptr && _len > 0; }
    
    /**
     * @brief Validates register size matches expected size
     * @param expectedSize Expected size in bytes
     * @return true if register size matches expected size
     */
    bool isValidSize(size_t expectedSize) const { return isValid() && _len == expectedSize; }
    
    /**
     * @brief Checks if register data appears to contain valid data (not all zeros or all ones)
     * @return true if data appears valid
     */
    bool hasValidData() const;
    
    /**
     * @brief Virtual method for register-specific validation (override in derived classes)
     * @return true if register content is semantically valid
     */
    virtual bool isSemanticallyValid() const { return isValid(); }

protected:
    
    /**
     * @brief Debug helper: dump raw register data to stream
     * @param s Output stream (default Serial)
     * @param prefix Optional prefix for the dump
     */
    void debugDumpRaw(Stream& s = Serial, const char* prefix = nullptr) const;

    /**
     * @brief Safely extracts a bitfield from the register data with validation
     * @param bitPos Bit offset of the field within the buffer
     * @param bitLen Number of bits in the field (field width, max 8)
     * @return The extracted value as an unsigned 8-bit integer, or 0 if invalid
     */
    uint8_t extractBits(uint8_t bitPos, uint8_t bitLen) const;

    /**
     * @brief Safely extracts a 16-bit bitfield from the register data with validation
     * @param bitPos Bit offset of the field within the buffer
     * @param bitLen Number of bits in the field (field width, max 16)
     * @return The extracted value as an unsigned 16-bit integer, or 0 if invalid
     */
    uint16_t extractBits16(uint8_t bitPos, uint8_t bitLen) const;

    /**
     * @brief Safely extracts a 32-bit bitfield from the register data with validation
     * @param bitPos Bit offset of the field within the buffer
     * @param bitLen Number of bits in the field (field width, max 32)
     * @return The extracted value as an unsigned 32-bit integer, or 0 if invalid
     */
    uint32_t extractBits32(uint8_t bitPos, uint8_t bitLen) const;

    /**
     * @brief Safely extracts a 64-bit bitfield from the register data with validation
     * @param bitPos Bit offset of the field within the buffer
     * @param bitLen Number of bits in the field (field width, max 64)
     * @return The extracted value as an unsigned 64-bit integer, or 0 if invalid
     */
    uint64_t extractBits64(uint8_t bitPos, uint8_t bitLen) const;

    /**
     * @brief Extracts a bitfield from a packed byte array with bounds checking.
     *
     * This utility function reads an arbitrary bitfield from a byte-aligned buffer.
     * It is designed to interpret register fields defined in the TPS25751 Technical Reference Manual.
     *
     * @param data      Pointer to the raw register buffer (e.g., 5 bytes from the Status register)
     * @param dataSize  Size of the data buffer in bytes for bounds checking
     * @param bitPos    Bit offset of the field within the buffer. This corresponds to the "Bit" column in tables
     *                  listed in Chapter 3 of the TPS25751 Technical Reference Manual.
     * @param bitLen    Number of bits in the field (field width, max 8)
     *
     * @return The extracted value as an unsigned 8-bit integer, or 0 if out of bounds
     */
    static uint8_t extractBits(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen);

    /**
     * @brief Extracts a 16-bit bitfield from a packed byte array with bounds checking.
     * @param data      Pointer to the raw register buffer
     * @param dataSize  Size of the data buffer in bytes for bounds checking
     * @param bitPos    Bit offset of the field within the buffer
     * @param bitLen    Number of bits in the field (field width, max 16)
     * @return The extracted value as an unsigned 16-bit integer, or 0 if out of bounds
     */
    static uint16_t extractBits16(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen);

    /**
     * @brief Extracts a 32-bit bitfield from a packed byte array with bounds checking.
     * @param data      Pointer to the raw register buffer
     * @param dataSize  Size of the data buffer in bytes for bounds checking
     * @param bitPos    Bit offset of the field within the buffer
     * @param bitLen    Number of bits in the field (field width, max 32)
     * @return The extracted value as an unsigned 32-bit integer, or 0 if out of bounds
     */
    static uint32_t extractBits32(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen);

    /**
     * @brief Extracts a 64-bit bitfield from a packed byte array with bounds checking.
     * @param data      Pointer to the raw register buffer
     * @param dataSize  Size of the data buffer in bytes for bounds checking
     * @param bitPos    Bit offset of the field within the buffer
     * @param bitLen    Number of bits in the field (field width, max 64)
     * @return The extracted value as an unsigned 64-bit integer, or 0 if out of bounds
     */
    static uint64_t extractBits64(const uint8_t *data, size_t dataSize, uint8_t bitPos, uint8_t bitLen);

    /**
     * @brief Safely converts extracted bits to enum with bounds checking (up to 8 bits)
     * @tparam EnumType The enum type to convert to
     * @param bitPos Bit offset of the field
     * @param bitLen Number of bits in the field (max 8)
     * @param maxValidValue Maximum valid enum value (inclusive)
     * @param defaultValue Default value to return if out of bounds
     * @return Valid enum value or defaultValue if out of bounds
     */
    template<typename EnumType>
    EnumType extractEnum(uint8_t bitPos, uint8_t bitLen, uint8_t maxValidValue, EnumType defaultValue) const
    {
        uint8_t rawValue = extractBits(bitPos, bitLen);
        if (rawValue > maxValidValue) {
            return defaultValue;
        }
        return static_cast<EnumType>(rawValue);
    }

    /**
     * @brief Safely converts extracted bits to enum with bounds checking (up to 16 bits)
     * @tparam EnumType The enum type to convert to
     * @param bitPos Bit offset of the field
     * @param bitLen Number of bits in the field (max 16)
     * @param maxValidValue Maximum valid enum value (inclusive)
     * @param defaultValue Default value to return if out of bounds
     * @return Valid enum value or defaultValue if out of bounds
     */
    template<typename EnumType>
    EnumType extractEnum16(uint8_t bitPos, uint8_t bitLen, uint16_t maxValidValue, EnumType defaultValue) const
    {
        uint16_t rawValue = extractBits16(bitPos, bitLen);
        if (rawValue > maxValidValue) {
            return defaultValue;
        }
        return static_cast<EnumType>(rawValue);
    }

    /**
     * @brief Safely converts extracted bits to enum with bounds checking (up to 32 bits)
     * @tparam EnumType The enum type to convert to
     * @param bitPos Bit offset of the field
     * @param bitLen Number of bits in the field (max 32)
     * @param maxValidValue Maximum valid enum value (inclusive)
     * @param defaultValue Default value to return if out of bounds
     * @return Valid enum value or defaultValue if out of bounds
     */
    template<typename EnumType>
    EnumType extractEnum32(uint8_t bitPos, uint8_t bitLen, uint32_t maxValidValue, EnumType defaultValue) const
    {
        uint32_t rawValue = extractBits32(bitPos, bitLen);
        if (rawValue > maxValidValue) {
            return defaultValue;
        }
        return static_cast<EnumType>(rawValue);
    }

    /**
     * @brief Safely converts extracted bits to enum with bounds checking (up to 64 bits)
     * @tparam EnumType The enum type to convert to
     * @param bitPos Bit offset of the field
     * @param bitLen Number of bits in the field (max 64)
     * @param maxValidValue Maximum valid enum value (inclusive)
     * @param defaultValue Default value to return if out of bounds
     * @return Valid enum value or defaultValue if out of bounds
     */
    template<typename EnumType>
    EnumType extractEnum64(uint8_t bitPos, uint8_t bitLen, uint64_t maxValidValue, EnumType defaultValue) const
    {
        uint64_t rawValue = extractBits64(bitPos, bitLen);
        if (rawValue > maxValidValue) {
            return defaultValue;
        }
        return static_cast<EnumType>(rawValue);
    }

    /**
     * @brief Extracts an array of 32-bit values from the register data
     * @param startBitPos Starting bit position for the first element
     * @param elementBitLen Bit length of each element (max 32)
     * @param numElements Number of elements to extract
     * @param output Pointer to output array (must have space for numElements)
     * @return Number of elements successfully extracted
     */
    size_t extractArray32(uint16_t startBitPos, uint8_t elementBitLen, size_t numElements, uint32_t* output) const;
};