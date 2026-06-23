#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @file TPS25751Task.h
 * @brief Value types for the TPS25751 4CC command-task interface (TRM Sec 4.4.2)
 *
 * The TPS25751 exposes a generic "4CC" task interface: the host writes an input
 * payload to DATA (0x09), writes a 4-character ASCII task code to COMMAND (0x08),
 * the controller runs the task, then clears COMMAND to 0 on success or replaces
 * it with "!CMD" if the code is unrecognized; results are read back from DATA.
 *
 * Kept in its own header (rather than folded into TPS25751Command.h) so that the
 * register class file stays one-class-per-file like its 15 siblings.
 */

/**
 * @brief A 4-character ASCII task code, as written to/read from COMMAND
 *
 * The TRM lists task codes as 4-character strings (e.g. "I2Cr"); on the wire the
 * first character occupies the lowest-addressed COMMAND byte, e.g. of("I2Cr")
 * produces code = {'I', '2', 'C', 'r'}.
 */
struct TPS25751FourCC {
    char code[4];

    /**
     * @brief Build a TPS25751FourCC from a 4-character string literal
     * @param s Null-terminated string of exactly 4 ASCII characters
     */
    static constexpr TPS25751FourCC of(const char (&s)[5]) {
        return TPS25751FourCC{{s[0], s[1], s[2], s[3]}};
    }
};

/// COMMAND value indicating no command is pending / the last command completed successfully
constexpr TPS25751FourCC TPS25751_CMD_CLEAR = TPS25751FourCC{{0, 0, 0, 0}};

/// COMMAND value the TPS25751 writes back when it does not recognize the requested 4CC
constexpr TPS25751FourCC TPS25751_CMD_REJECTED = TPS25751FourCC::of("!CMD");

/**
 * @brief Outcome of polling COMMAND after issuing a 4CC task
 */
enum class TPS25751TaskStatus : uint8_t {
    Success,    ///< COMMAND cleared to all-zero before timeout
    Rejected,   ///< COMMAND read back as "!CMD" (unrecognized task code)
    Timeout,    ///< Neither clear nor "!CMD" observed before timeoutMs elapsed
    I2CError    ///< A bus transaction (write or poll read) failed
};

/**
 * @brief Result of executeCommand(): completion status + the task's return code
 *
 * returnCode is DATA byte 0 after a Success completion (the TI task return code,
 * e.g. 0 = success for most tasks); it is unparsed/task-specific here and left to
 * the caller (or a downstream-device codec) to interpret. Undefined for
 * non-Success statuses.
 */
struct TPS25751TaskResult {
    TPS25751TaskStatus status;
    uint8_t returnCode;
};
