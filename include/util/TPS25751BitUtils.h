#ifndef TPS25751BITUTILS_H
#define TPS25751BITUTILS_H

#include <cstdint>

/**
 * @brief Extracts a bitfield from a packed byte array.
 *
 * This utility function reads an arbitrary bitfield from a byte-aligned buffer.
 * It is designed to interpret register fields defined in the TPS25751 Technical Reference Manual.
 *
 * @param data   Pointer to the raw register buffer (e.g., 5 bytes from the Status register)
 * @param bitPos Bit offset of the field within the buffer. This corresponds to the "Bit" column in tables
 *               listed in Chapter 3 of the TPS25751 Technical Reference Manual.
 * @param bitLen Number of bits in the field (field width)
 *
 * @return The extracted value as an unsigned 8-bit integer
 */
uint8_t extractBits(const uint8_t *data, uint8_t bitPos, uint8_t bitLen);

#endif // TPS25751BITUTILS_H
