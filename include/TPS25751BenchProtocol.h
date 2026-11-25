#pragma once

/**
 * @file TPS25751BenchProtocol.h
 * @brief Line protocol of the TPS25751 bench console: parsing, allowlists, formatting
 *
 * The bench console (TPS25751BenchConsole) lets a host program read registers,
 * run 4CC tasks, and restart the controller over a serial port. This file holds
 * the part of it that touches neither I2C nor a clock, so the native unit tests
 * cover it without hardware: the request parser, the task and write allowlists,
 * and the reply formatting. The host side is the `tps25751-pd-bench` MCP server
 * in tools/pd-bench.
 *
 * Protocol version 1:
 *
 * @code
 * request  @<id> <COMMAND> [arguments]
 * reply    @<id> <text>                  zero or more lines
 * final    @<id> OK
 *          @<id> ERR <CODE> <text>
 * event    ! <text>                      not tied to a request
 * @endcode
 *
 * The id is 1 to 8 letters or digits chosen by the host and echoed on every
 * reply line. A line that starts with neither `@` nor `!` is log output from the
 * rest of the firmware and is not part of the protocol. Reply text is either
 * space-separated `key=value` tokens or, for decoded registers, `| ` followed by
 * one line of the library's debugPrint() output. Hex is lowercase.
 *
 * | Command | Reply |
 * |---|---|
 * | `HELLO` | Protocol and library version, build date, uptime, MODE, VERSION, role, patch bundle; reads only |
 * | `RESTART warm` or `cold` | Runs `Gaid` or `GAID`, waits for the controller, reloads the patch |
 * | `RD <addr>` | `reg=0x34 len=6 data=d64106009800`: the length byte as read, then the data |
 * | `DEC <addr>` | The `RD` line, then the register's debugPrint() output |
 * | `SNAP` | One `RD` line per snapshot register, then clears INT_EVENT1 |
 * | `CMD <4cc> [hex]` | Task status, return code, and the 64 DATA bytes |
 * | `WR <addr> <hex>` | Writes one of the allowed registers and returns the readback |
 *
 * This is not a register class: it does not inherit TPS25751Register and is not
 * part of the register factory.
 */

#include <Arduino.h>
#include <Stream.h>
#include "TPS25751Task.h"

/// True when the firmware was built with -D TPS25751_BENCH_ALLOW_SWAPS, which
/// lets `CMD` run the four role-swap tasks.
#ifdef TPS25751_BENCH_ALLOW_SWAPS
constexpr bool TPS25751_BENCH_SWAPS_ALLOWED = true;
#else
constexpr bool TPS25751_BENCH_SWAPS_ALLOWED = false;
#endif

/// The commands of protocol version 1.
enum class TPS25751BenchCommand : uint8_t {
    None,      ///< No command parsed
    Hello,     ///< `HELLO`
    Restart,   ///< `RESTART warm` or `RESTART cold`
    Read,      ///< `RD <addr>`
    Decode,    ///< `DEC <addr>`
    Snapshot,  ///< `SNAP`
    Task,      ///< `CMD <4cc> [hex]`
    Write      ///< `WR <addr> <hex>`
};

/// The error codes a reply can carry after `ERR`.
enum class TPS25751BenchError : uint8_t {
    None,            ///< No error
    Syntax,          ///< `SYNTAX`: the line is not `@<id> <COMMAND> ...`
    TooLong,         ///< `TOOLONG`: the line exceeds the console's buffer
    UnknownCommand,  ///< `UNKNOWN`: the command word is not in this protocol version
    BadArgument,     ///< `BADARG`: a missing, extra, or malformed argument
    Refused,         ///< `REFUSED`: the task or register is not on the allowlist
    I2C,             ///< `I2C`: the controller did not answer
    Timeout,         ///< `TIMEOUT`: a task or a restart did not finish in time
    Rejected,        ///< `REJECTED`: the controller answered the task with "!CMD"
    NoBundle,        ///< `NOBUNDLE`: a patch bundle is needed and none is compiled in
    Patch,           ///< `PATCH`: loading the patch bundle failed
    NoDecoder        ///< `NODECODER`: the library has no register class for the address
};

/// What the console does with a 4CC task code given to `CMD`.
enum class TPS25751BenchTaskPolicy : uint8_t {
    Allowed,                 ///< Run it through TPS25751::executeCommand()
    Restart,                 ///< `Gaid` or `GAID`: run the restart sequence of `RESTART`
    RefusedPatch,            ///< `PBMs`, `PBMc`, `PBMe`: patch loading belongs to the console
    RefusedPatchMode,        ///< `GO2P`: forces the controller back to PTCH mode
    RefusedFlash,            ///< `FLrd`, `FLad`, `FLwd`, `FLvy`: external EEPROM access
    RefusedDownstreamWrite,  ///< `I2Cw`: writes a device on the I2Cc bus
    RefusedSwap,             ///< A role swap, in a build without TPS25751_BENCH_ALLOW_SWAPS
    RefusedUnlisted          ///< Any other code
};

/**
 * @brief One parsed request line
 *
 * parse() fills only the fields the command uses; the rest keep their defaults.
 */
struct TPS25751BenchRequest {
    static constexpr size_t kMaxIdLength = 8;   ///< Longest request id, in characters
    static constexpr size_t kMaxPayload = 64;   ///< Longest hex argument, in bytes (the DATA register)

    char id[kMaxIdLength + 1] = {0};            ///< Request id, empty when the line had none
    TPS25751BenchCommand command = TPS25751BenchCommand::None;
    uint8_t address = 0;                        ///< Register address of `RD`, `DEC`, `WR`
    TPS25751FourCC task = TPS25751_CMD_CLEAR;   ///< Task code of `CMD`
    bool cold = false;                          ///< `RESTART cold`
    uint8_t payload[kMaxPayload] = {0};         ///< Bytes of the hex argument of `CMD` or `WR`
    uint8_t payloadLength = 0;                  ///< Number of bytes in payload
};

/**
 * @brief Parsing, allowlists, and reply formatting for the bench console
 *
 * Every member is static and free of I2C and timing, so the class is a namespace
 * with access control. TPS25751BenchConsole binds it to a controller and a port.
 *
 * @code
 * TPS25751BenchRequest request;
 * TPS25751BenchError error = TPS25751BenchProtocol::parse("@7 RD 0x34", request);
 * if (error == TPS25751BenchError::None) {
 *     // request.command == TPS25751BenchCommand::Read, request.address == 0x34
 * }
 * @endcode
 */
class TPS25751BenchProtocol {
public:
    /// Protocol version reported by `HELLO`. The host refuses any other version.
    static constexpr uint8_t kVersion = 1;

    /// Longest request line the console accepts, without the line feed. The
    /// longest valid line is `@<8-char id> CMD <4cc> <128 hex digits>`, 147
    /// characters.
    static constexpr size_t kMaxLineLength = 160;

    /// Number of registers in a snapshot.
    static constexpr size_t kSnapshotRegisterCount = 17;

    /**
     * @brief Registers `SNAP` reads, in order
     *
     * MODE (0x03), INT_EVENT1 (0x14), STATUS (0x1A), POWER_PATH_STATUS (0x26),
     * the capabilities and the contract (0x30 to 0x35), AUTONEGOTIATE_SINK
     * (0x37), POWER_STATUS (0x3F), PD_STATUS (0x40), PD3_CONFIGURATION (0x42),
     * RECEIVED_SOP_IDENTITY (0x48), TYPEC_STATE (0x69), and ADC_RESULTS (0x6A).
     */
    static constexpr uint8_t kSnapshotRegisters[kSnapshotRegisterCount] = {
        0x03, 0x14, 0x1A, 0x26, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
        0x37, 0x3F, 0x40, 0x42, 0x48, 0x69, 0x6A};

    /**
     * @brief Parse one request line
     *
     * The line must not contain the line feed. A trailing carriage return and
     * trailing spaces are ignored. The command word is matched without regard
     * to case; a 4CC task code is case-sensitive, because `Gaid` and `GAID` are
     * different tasks.
     *
     * @param line    Null-terminated request line
     * @param request Receives the parsed request. Its id is filled whenever the
     *                line carried a valid one, including when parsing fails
     *                later, so the error reply can echo it.
     * @return TPS25751BenchError::None on success, otherwise Syntax,
     *         UnknownCommand, or BadArgument
     */
    static TPS25751BenchError parse(const char *line, TPS25751BenchRequest &request);

    /**
     * @brief Decide what `CMD` does with a task code
     *
     * Allowed: `GSrC`, `GSkC`, `ANeg`, `SSrC`, `GPPI`, `MBRd`, `SRDY`, `SRYR`,
     * `DBfg`, `I2Cr`. `Gaid` and `GAID` are allowed and run the restart
     * sequence. The swaps `SWSk`, `SWSr`, `SWDF`, `SWUF` are allowed only when
     * @p allowSwaps is true. Everything else is refused, including the patch
     * tasks, `GO2P`, the EEPROM tasks, `I2Cw`, and the GPIO tasks.
     *
     * @param code       The 4CC task code
     * @param allowSwaps Whether the role swaps are allowed; defaults to the
     *                   build's TPS25751_BENCH_ALLOW_SWAPS setting
     * @return The policy for the code
     *
     * @see SLVUCR8B section 4, 4CC Task Detailed Descriptions (p.61 to 86)
     */
    static TPS25751BenchTaskPolicy taskPolicy(TPS25751FourCC code,
                                              bool allowSwaps = TPS25751_BENCH_SWAPS_ALLOWED);

    /**
     * @brief Reason text for a refusing policy
     * @param policy A policy returned by taskPolicy()
     * @return A short reason for the `ERR REFUSED` reply; empty for the two
     *         allowing policies
     */
    static const __FlashStringHelper *refusalText(TPS25751BenchTaskPolicy policy);

    /**
     * @brief How long the console waits for a task's COMMAND register to clear
     *
     * The PD message tasks run "at the first opportunity while maintaining
     * Policy Engine compliance" (SLVUCR8B p.65) and then wait for the partner's
     * reply, so they get 2 s. `GPPI` gets 5 s: "The task may sometimes take long
     * time to complete" (p.67). The other allowed tasks finish within the
     * controller and get 1 s.
     *
     * @param code The 4CC task code
     * @return Timeout in milliseconds for TPS25751::executeCommand()
     */
    static uint32_t taskTimeoutMs(TPS25751FourCC code);

    /**
     * @brief Whether `WR` may write a register
     *
     * Allowed: GLOBAL_SYSTEM_CONFIGURATION (0x27), PORT_CONFIGURATION (0x28),
     * PORT_CONTROL (0x29), AUTONEGOTIATE_SINK (0x37), and PD3_CONFIGURATION
     * (0x42), each as a whole register. A write to 0x27 or 0x28 disconnects and
     * reconnects the port. A write to 0x29 does not; it takes effect the next
     * time the policy it governs runs (SLVUCR8B p.31), such as whether the port
     * next initiates or accepts a PR_Swap (bits 5 and 4).
     *
     * @param address Register address
     * @return true for the five addresses above
     */
    static bool isWriteAllowed(uint8_t address);

    /**
     * @brief The code word printed after `ERR`
     * @param error An error other than None
     * @return An uppercase word such as "BADARG"; "NONE" for None
     */
    static const char *errorCode(TPS25751BenchError error);

    /**
     * @brief Compare two task codes
     * @return true if all four characters match
     */
    static bool sameTask(TPS25751FourCC a, TPS25751FourCC b);

    // ----- reply formatting -----

    /// Start a reply line: prints `@<id> `. An empty id prints `@? `.
    static void beginLine(Stream &out, const char *id);

    /// Print `@<id> OK` and end the line.
    static void writeOk(Stream &out, const char *id);

    /**
     * @brief Start an error reply: prints `@<id> ERR <CODE> `
     *
     * The caller prints the text and ends the line, which lets it add numbers.
     */
    static void beginError(Stream &out, const char *id, TPS25751BenchError error);

    /// Print a complete error reply with fixed text.
    static void writeError(Stream &out, const char *id, TPS25751BenchError error,
                           const __FlashStringHelper *text);

    /// Print an event line: `! <text>`.
    static void writeEvent(Stream &out, const __FlashStringHelper *text);

    /// Print a register address as `0x` and two lowercase hex digits.
    static void printAddress(Stream &out, uint8_t address);

    /// Print bytes as lowercase hex without separators.
    static void printHex(Stream &out, const uint8_t *data, size_t length);

    /// Print a 4CC code. A character outside printable ASCII prints as `?`.
    static void printTask(Stream &out, TPS25751FourCC code);

    /**
     * @brief Print the tokens of a register line: `reg=0x34 len=6 data=d64106009800`
     *
     * The caller starts and ends the line. @p lengthByte is the byte the
     * controller sent, which the `tps25751-registers` format stores as read.
     *
     * @param out        Output stream
     * @param address    Register address
     * @param lengthByte Length byte the controller returned
     * @param data       Data bytes that followed the length byte
     * @param dataLength Number of bytes in @p data
     */
    static void printRegister(Stream &out, uint8_t address, uint8_t lengthByte,
                              const uint8_t *data, size_t dataLength);

private:
    static bool parseAddress(const char *&cursor, uint8_t &address);
    static bool parseHexPayload(const char *&cursor, TPS25751BenchRequest &request);
    static bool parseTask(const char *&cursor, TPS25751FourCC &task);
    static void skipSpaces(const char *&cursor);
    static bool atEnd(const char *cursor);
    static bool wordEquals(const char *word, size_t length, const char *expected);
    static int hexValue(char c);
};

/**
 * @brief A Stream that prefixes every line with `@<id> | `
 *
 * `DEC` passes one to a register's debugPrint(), so the library's multi-line
 * decode arrives as reply lines of the request. It forwards output only; its
 * input side is empty.
 *
 * @code
 * TPS25751BenchLinePrefixer lines(SerialUSB1, "7");
 * status.debugPrint(lines);   // each line arrives as "@7 | ..."
 * lines.finish();
 * @endcode
 */
class TPS25751BenchLinePrefixer : public Stream {
public:
    /**
     * @param out Stream that receives the prefixed lines
     * @param id  Request id; the pointer must stay valid while this object is used
     */
    TPS25751BenchLinePrefixer(Stream &out, const char *id);

    using Stream::write;

    /// Forward one character, starting the line with the prefix when needed.
    size_t write(uint8_t c) override;

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

    /// End a line that the source left without a line feed.
    void finish();

private:
    Stream &_out;
    const char *_id;
    bool _lineOpen = false;
};
