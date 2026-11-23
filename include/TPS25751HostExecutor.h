#pragma once

/**
 * @file TPS25751HostExecutor.h
 * @brief Adapts a TPS25751 host to the TPS25751CommandExecutor interface
 *
 * A pass-through: each call forwards to the matching TPS25751 method, and the
 * clock uses Arduino delay() and millis(). Like TPS25751.cpp, its source needs
 * Wire and is excluded from the native test build.
 *
 * @code
 * TPS25751 pd;
 * TPS25751HostExecutor exec(pd);
 * TPS25751EepromUpdater updater(exec);
 * @endcode
 *
 * @see TPS25751CommandExecutor
 */

#include "TPS25751CommandExecutor.h"

class TPS25751;  ///< forward declaration; TPS25751HostExecutor.cpp includes the full type

/**
 * @brief TPS25751CommandExecutor backed by a real TPS25751 on I2C
 */
class TPS25751HostExecutor : public TPS25751CommandExecutor {
public:
    /**
     * @brief Bind the adapter to a TPS25751 host
     * @param pd The TPS25751 instance to forward to (must outlive this object)
     */
    explicit TPS25751HostExecutor(TPS25751& pd);

    /**
     * @brief Forward to TPS25751::executeCommand()
     * @param cmd       4-character task code
     * @param inData    Input payload for DATA, or nullptr
     * @param inLen     Length of @p inData in bytes
     * @param outData   Buffer for DATA after completion, or nullptr
     * @param outLen    Size of @p outData in bytes
     * @param timeoutMs Maximum time to wait for COMMAND to clear or reject
     * @return The result TPS25751::executeCommand() returned
     */
    TPS25751TaskResult executeCommand(TPS25751FourCC cmd,
        const uint8_t* inData, size_t inLen,
        uint8_t* outData, size_t outLen, uint32_t timeoutMs) override;

    /**
     * @brief Forward to TPS25751::readMode()
     * @param mode Register object to populate
     * @return true if the read succeeded
     */
    bool readMode(TPS25751Mode& mode) override;

    /**
     * @brief Forward to TPS25751::readBootFlags()
     * @param flags Register object to populate
     * @return true if the read succeeded
     */
    bool readBootFlags(TPS25751BootFlags& flags) override;

    /**
     * @brief Arduino delay()
     * @param ms Delay in milliseconds
     */
    void delayMs(uint32_t ms) override;

    /**
     * @brief Arduino millis()
     * @return Milliseconds since boot
     */
    uint32_t nowMs() override;

private:
    TPS25751& _pd;
};
