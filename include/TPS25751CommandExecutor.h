#pragma once

/**
 * @file TPS25751CommandExecutor.h
 * @brief Abstract host interface that sequencers such as TPS25751EepromUpdater drive
 *
 * TPS25751EepromUpdater needs four things from the host: the 4CC task executor,
 * the MODE and BOOT_STATUS reads, and a clock. TPS25751 provides all of them, but
 * src/TPS25751.cpp needs Wire and is not part of the native test build. This
 * interface is the seam between the two: TPS25751HostExecutor adapts a TPS25751 to
 * it on the target, and the native tests supply a scripted fake.
 *
 * The header deliberately does not include Wire.h. It uses virtual functions
 * only, which need no RTTI, so the -fno-rtti platform constraint holds.
 *
 * @see TPS25751HostExecutor
 * @see TPS25751EepromUpdater
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B, Chapter 4 (4CC tasks)
 */

#include <stdint.h>
#include <stddef.h>
#include "TPS25751Task.h"  // TPS25751FourCC, TPS25751TaskResult

class TPS25751Mode;
class TPS25751BootFlags;

/**
 * @brief Host operations a 4CC task sequencer depends on
 *
 * Implementations: TPS25751HostExecutor (hardware, wraps TPS25751) and the native
 * test fake in test/support/native/FakeCommandExecutor.h.
 */
class TPS25751CommandExecutor {
public:
    virtual ~TPS25751CommandExecutor() = default;

    /**
     * @brief Run one 4CC task through COMMAND and DATA
     *
     * Same contract as TPS25751::executeCommand(): @p inData is written to DATA,
     * @p cmd to COMMAND, COMMAND is polled until it clears or reads "!CMD", and on
     * success @p outData is filled from DATA byte 0 (the I2C length byte already
     * stripped).
     *
     * @param cmd       4-character task code
     * @param inData    Input payload for DATA, or nullptr for none
     * @param inLen     Length of @p inData in bytes (0 to 64)
     * @param outData   Buffer for DATA after completion, or nullptr
     * @param outLen    Size of @p outData in bytes
     * @param timeoutMs Maximum time to wait for COMMAND to clear or reject
     * @return Completion status and DATA byte 0 as the return code
     * @see TPS25751::executeCommand()
     */
    virtual TPS25751TaskResult executeCommand(TPS25751FourCC cmd,
        const uint8_t* inData, size_t inLen,
        uint8_t* outData, size_t outLen, uint32_t timeoutMs) = 0;

    /**
     * @brief Read the MODE register (0x03)
     * @param mode Register object to populate
     * @return true if the read succeeded
     */
    virtual bool readMode(TPS25751Mode& mode) = 0;

    /**
     * @brief Read the BOOT_STATUS register (0x2D, "Boot Flags")
     * @param flags Register object to populate
     * @return true if the read succeeded
     */
    virtual bool readBootFlags(TPS25751BootFlags& flags) = 0;

    /**
     * @brief Block for @p ms milliseconds
     * @param ms Delay in milliseconds
     */
    virtual void delayMs(uint32_t ms) = 0;

    /**
     * @brief Milliseconds since an arbitrary epoch, wrapping like millis()
     * @return Current time in milliseconds
     */
    virtual uint32_t nowMs() = 0;
};
