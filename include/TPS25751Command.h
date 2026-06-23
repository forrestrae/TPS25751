#ifndef TPS25751COMMAND_H
#define TPS25751COMMAND_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Command Register (COMMAND, Address: 0x08)
 *
 * Write a 4-character ASCII task code ("4CC") here to start a command-task (see
 * TPS25751Task.h / TPS25751::executeCommand()). The controller clears this
 * register to all-zero on success, or replaces it with "!CMD" if the code is
 * unrecognized. Input/output for the task is exchanged via the DATA register
 * (0x09, see TPS25751Data.h).
 *
 * Register size: 4 bytes
 * Access: Write (read-back reflects in-progress/last command state)
 *
 * @see TI TPS25751 Technical Reference Manual, Section 4.4.2
 */
class TPS25751Command : public TPS25751Register
{
public:
    explicit TPS25751Command() : TPS25751Register(TPS25751Registers::Registers::COMMAND.size) {};
    explicit TPS25751Command(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::COMMAND.size) {};
    TPS25751Command(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751Command(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    /**
     * @brief Get the raw 4-character command code as a null-terminated string
     * @return Pointer to an internal static buffer holding the 4CC + terminator
     *         (mirrors TPS25751Mode::modeString()); empty string if invalid.
     */
    const char* fourCC() const;

    /**
     * @brief Check whether COMMAND is clear (all-zero), i.e. no command pending /
     *        the last command completed successfully
     */
    bool isClear() const;

    /**
     * @brief Check whether COMMAND reads back as the rejection code "!CMD"
     */
    bool isRejected() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates the command register semantically
     *
     * Valid states are: clear (all-zero), the rejection code "!CMD", or any
     * 4-character printable-ASCII task code.
     * @return true if the register content is one of the above
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751COMMAND_H
