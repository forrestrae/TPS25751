#ifndef TPS25751DATA_H
#define TPS25751DATA_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Data Register (DATA, Address: 0x09)
 *
 * Generic 64-byte scratch register used to exchange input/output payloads for
 * 4CC command-tasks (see TPS25751Task.h / TPS25751::executeCommand()). Unlike
 * the other 15 registers, DATA has no fixed field layout of its own — its
 * interpretation depends entirely on which task is in flight (e.g. for the
 * I2Cr/I2Cw downstream-proxy tasks, see TPS25751DownstreamDevice.h). This class
 * therefore exposes only generic byte/bitfield access and a hexdump
 * debugPrint(), reusing the base class's extractBits*() helpers for callers
 * that know a specific task's layout.
 *
 * Register size: 64 bytes (largest register in the map)
 * Access: Read/Write
 *
 * @see TI TPS25751 Technical Reference Manual, Section 4.4.2/4.4.3
 */
class TPS25751Data : public TPS25751Register
{
public:
    explicit TPS25751Data() : TPS25751Register(TPS25751Registers::Registers::DATA.size) {};
    explicit TPS25751Data(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::DATA.size) {};
    TPS25751Data(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751Data(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    /**
     * @brief Get a single raw byte from the register
     * @param index Byte offset (0-based)
     * @return The byte at @p index, or 0 if out of range / invalid
     */
    uint8_t byteAt(size_t index) const;

    /**
     * @brief Safely extract an 8-bit bitfield (exposes the protected base helper)
     * @param bitPos Bit offset within the buffer
     * @param bitLen Number of bits in the field (max 8)
     */
    uint8_t extractByteField(uint8_t bitPos, uint8_t bitLen) const { return extractBits(bitPos, bitLen); }

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates the data register semantically
     *
     * DATA has no fixed field layout, so semantic validation is limited to the
     * generic checks the base class already performs (non-null, correctly
     * sized buffer).
     * @return true if the register has valid, correctly-sized backing storage
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751DATA_H
