#include "TPS25751BenchProtocol.h"

#include <string.h>

namespace {

// Task timeouts, see taskTimeoutMs().
constexpr uint32_t kPdMessageTaskTimeoutMs = 2000;
constexpr uint32_t kGetMessageTaskTimeoutMs = 5000;
constexpr uint32_t kLocalTaskTimeoutMs = 1000;

// The registers `WR` may write. 0x27 and 0x28 disconnect and reconnect the
// port when written (SLVUCR8B p.28, p.29); 0x37 steers the sink request (p.40);
// 0x42 enables the PD 3.x replies (p.46).
constexpr uint8_t kWritableRegisters[] = {0x27, 0x28, 0x37, 0x42};

constexpr size_t kMaxCommandWordLength = 8;

char toUpperAscii(char c)
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

bool isAlphaNumeric(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isTask(TPS25751FourCC code, const char (&name)[5])
{
    return TPS25751BenchProtocol::sameTask(code, TPS25751FourCC::of(name));
}

}  // namespace

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

void TPS25751BenchProtocol::skipSpaces(const char *&cursor)
{
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
}

bool TPS25751BenchProtocol::atEnd(const char *cursor)
{
    skipSpaces(cursor);
    return *cursor == '\0' || *cursor == '\r';
}

int TPS25751BenchProtocol::hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool TPS25751BenchProtocol::wordEquals(const char *word, size_t length, const char *expected)
{
    if (strlen(expected) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (toUpperAscii(word[i]) != expected[i]) return false;
    }
    return true;
}

bool TPS25751BenchProtocol::parseAddress(const char *&cursor, uint8_t &address)
{
    skipSpaces(cursor);
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) cursor += 2;

    unsigned value = 0;
    size_t digits = 0;
    while (hexValue(*cursor) >= 0) {
        value = (value << 4) | static_cast<unsigned>(hexValue(*cursor));
        ++cursor;
        if (++digits > 2) return false;
    }
    if (digits == 0) return false;
    // The address must end at a space or at the end of the line.
    if (*cursor != ' ' && *cursor != '\t' && *cursor != '\0' && *cursor != '\r') return false;
    address = static_cast<uint8_t>(value);
    return true;
}

bool TPS25751BenchProtocol::parseHexPayload(const char *&cursor, TPS25751BenchRequest &request)
{
    skipSpaces(cursor);
    size_t length = 0;
    while (hexValue(cursor[0]) >= 0) {
        const int low = hexValue(cursor[1]);
        if (low < 0) return false;  // odd number of digits
        if (length >= TPS25751BenchRequest::kMaxPayload) return false;
        request.payload[length++] = static_cast<uint8_t>((hexValue(cursor[0]) << 4) | low);
        cursor += 2;
    }
    if (*cursor != ' ' && *cursor != '\t' && *cursor != '\0' && *cursor != '\r') return false;
    request.payloadLength = static_cast<uint8_t>(length);
    return true;
}

bool TPS25751BenchProtocol::parseTask(const char *&cursor, TPS25751FourCC &task)
{
    skipSpaces(cursor);
    for (size_t i = 0; i < 4; ++i) {
        const char c = cursor[i];
        if (c <= ' ' || c > '~') return false;  // also stops at the terminator
        task.code[i] = c;
    }
    cursor += 4;
    return *cursor == ' ' || *cursor == '\t' || *cursor == '\0' || *cursor == '\r';
}

TPS25751BenchError TPS25751BenchProtocol::parse(const char *line, TPS25751BenchRequest &request)
{
    request = TPS25751BenchRequest();
    if (line == nullptr) return TPS25751BenchError::Syntax;

    const char *cursor = line;
    skipSpaces(cursor);
    if (*cursor != '@') return TPS25751BenchError::Syntax;
    ++cursor;

    // Request id: 1 to 8 letters or digits, ended by a space or the line end.
    size_t idLength = 0;
    while (isAlphaNumeric(cursor[idLength])) {
        if (++idLength > TPS25751BenchRequest::kMaxIdLength) return TPS25751BenchError::Syntax;
    }
    const char afterId = cursor[idLength];
    if (idLength == 0 || (afterId != ' ' && afterId != '\t' && afterId != '\0' && afterId != '\r')) {
        return TPS25751BenchError::Syntax;
    }
    memcpy(request.id, cursor, idLength);
    request.id[idLength] = '\0';
    cursor += idLength;

    // Command word.
    skipSpaces(cursor);
    const char *word = cursor;
    size_t wordLength = 0;
    while (isAlphaNumeric(cursor[wordLength])) ++wordLength;
    if (wordLength == 0) return TPS25751BenchError::Syntax;
    const char afterWord = cursor[wordLength];
    if (afterWord != ' ' && afterWord != '\t' && afterWord != '\0' && afterWord != '\r') {
        return TPS25751BenchError::Syntax;
    }
    if (wordLength > kMaxCommandWordLength) return TPS25751BenchError::UnknownCommand;
    cursor += wordLength;

    if (wordEquals(word, wordLength, "HELLO")) {
        request.command = TPS25751BenchCommand::Hello;
    } else if (wordEquals(word, wordLength, "SNAP")) {
        request.command = TPS25751BenchCommand::Snapshot;
    } else if (wordEquals(word, wordLength, "RESTART")) {
        request.command = TPS25751BenchCommand::Restart;
        skipSpaces(cursor);
        const char *mode = cursor;
        size_t modeLength = 0;
        while (isAlphaNumeric(cursor[modeLength])) ++modeLength;
        if (wordEquals(mode, modeLength, "COLD")) {
            request.cold = true;
        } else if (!wordEquals(mode, modeLength, "WARM")) {
            return TPS25751BenchError::BadArgument;
        }
        cursor += modeLength;
    } else if (wordEquals(word, wordLength, "RD") || wordEquals(word, wordLength, "DEC")) {
        request.command = (wordLength == 2) ? TPS25751BenchCommand::Read
                                            : TPS25751BenchCommand::Decode;
        if (!parseAddress(cursor, request.address)) return TPS25751BenchError::BadArgument;
    } else if (wordEquals(word, wordLength, "CMD")) {
        request.command = TPS25751BenchCommand::Task;
        if (!parseTask(cursor, request.task)) return TPS25751BenchError::BadArgument;
        if (!parseHexPayload(cursor, request)) return TPS25751BenchError::BadArgument;
    } else if (wordEquals(word, wordLength, "WR")) {
        request.command = TPS25751BenchCommand::Write;
        if (!parseAddress(cursor, request.address)) return TPS25751BenchError::BadArgument;
        if (!parseHexPayload(cursor, request)) return TPS25751BenchError::BadArgument;
        if (request.payloadLength == 0) return TPS25751BenchError::BadArgument;
    } else {
        return TPS25751BenchError::UnknownCommand;
    }

    // Nothing may follow the last argument.
    if (!atEnd(cursor)) return TPS25751BenchError::BadArgument;
    return TPS25751BenchError::None;
}

// ---------------------------------------------------------------------------
// Allowlists
// ---------------------------------------------------------------------------

bool TPS25751BenchProtocol::sameTask(TPS25751FourCC a, TPS25751FourCC b)
{
    return memcmp(a.code, b.code, sizeof(a.code)) == 0;
}

TPS25751BenchTaskPolicy TPS25751BenchProtocol::taskPolicy(TPS25751FourCC code, bool allowSwaps)
{
    // PD message tasks (SLVUCR8B section 4.3, p.65 to 73), the power switch
    // tasks (section 4.4, p.74 to 75), and three system tasks (section 4.6,
    // p.83 to 85).
    if (isTask(code, "GSrC") || isTask(code, "GSkC") || isTask(code, "ANeg") ||
        isTask(code, "SSrC") || isTask(code, "GPPI") || isTask(code, "MBRd") ||
        isTask(code, "SRDY") || isTask(code, "SRYR") || isTask(code, "DBfg") ||
        isTask(code, "I2Cr")) {
        return TPS25751BenchTaskPolicy::Allowed;
    }
    // CPU control tasks (section 4.2, p.62): the controller restarts, so they
    // need the restart sequence and not the plain executor.
    if (isTask(code, "Gaid") || isTask(code, "GAID")) {
        return TPS25751BenchTaskPolicy::Restart;
    }
    // Role swaps (sections 4.3.1 to 4.3.4, p.63 to 64). A PR_Swap moves the
    // VBUS source, so they need an explicit build flag.
    if (isTask(code, "SWSk") || isTask(code, "SWSr") || isTask(code, "SWDF") ||
        isTask(code, "SWUF")) {
        return allowSwaps ? TPS25751BenchTaskPolicy::Allowed
                          : TPS25751BenchTaskPolicy::RefusedSwap;
    }
    // Patch bundle tasks (section 4.5, p.76 to 80).
    if (isTask(code, "PBMs") || isTask(code, "PBMc") || isTask(code, "PBMe")) {
        return TPS25751BenchTaskPolicy::RefusedPatch;
    }
    if (isTask(code, "GO2P")) {
        return TPS25751BenchTaskPolicy::RefusedPatchMode;
    }
    // External EEPROM tasks (p.81 to 82).
    if (isTask(code, "FLrd") || isTask(code, "FLad") || isTask(code, "FLwd") ||
        isTask(code, "FLvy")) {
        return TPS25751BenchTaskPolicy::RefusedFlash;
    }
    if (isTask(code, "I2Cw")) {
        return TPS25751BenchTaskPolicy::RefusedDownstreamWrite;
    }
    return TPS25751BenchTaskPolicy::RefusedUnlisted;
}

const __FlashStringHelper *TPS25751BenchProtocol::refusalText(TPS25751BenchTaskPolicy policy)
{
    switch (policy) {
        case TPS25751BenchTaskPolicy::RefusedPatch:
            return F("patch tasks are run by the console's own patch loader");
        case TPS25751BenchTaskPolicy::RefusedPatchMode:
            return F("GO2P forces the controller back to PTCH mode");
        case TPS25751BenchTaskPolicy::RefusedFlash:
            return F("external EEPROM tasks are not allowed");
        case TPS25751BenchTaskPolicy::RefusedDownstreamWrite:
            return F("I2Cw writes a device on the I2Cc bus");
        case TPS25751BenchTaskPolicy::RefusedSwap:
            return F("role swaps need a build with -D TPS25751_BENCH_ALLOW_SWAPS");
        case TPS25751BenchTaskPolicy::RefusedUnlisted:
            return F("task is not on the allowlist");
        default:
            return F("");
    }
}

uint32_t TPS25751BenchProtocol::taskTimeoutMs(TPS25751FourCC code)
{
    if (isTask(code, "GPPI")) return kGetMessageTaskTimeoutMs;
    if (isTask(code, "GSrC") || isTask(code, "GSkC") || isTask(code, "ANeg") ||
        isTask(code, "SSrC") || isTask(code, "SWSk") || isTask(code, "SWSr") ||
        isTask(code, "SWDF") || isTask(code, "SWUF")) {
        return kPdMessageTaskTimeoutMs;
    }
    return kLocalTaskTimeoutMs;
}

bool TPS25751BenchProtocol::isWriteAllowed(uint8_t address)
{
    for (uint8_t allowed : kWritableRegisters) {
        if (allowed == address) return true;
    }
    return false;
}

const char *TPS25751BenchProtocol::errorCode(TPS25751BenchError error)
{
    switch (error) {
        case TPS25751BenchError::Syntax:         return "SYNTAX";
        case TPS25751BenchError::TooLong:        return "TOOLONG";
        case TPS25751BenchError::UnknownCommand: return "UNKNOWN";
        case TPS25751BenchError::BadArgument:    return "BADARG";
        case TPS25751BenchError::Refused:        return "REFUSED";
        case TPS25751BenchError::I2C:            return "I2C";
        case TPS25751BenchError::Timeout:        return "TIMEOUT";
        case TPS25751BenchError::Rejected:       return "REJECTED";
        case TPS25751BenchError::NoBundle:       return "NOBUNDLE";
        case TPS25751BenchError::Patch:          return "PATCH";
        case TPS25751BenchError::NoDecoder:      return "NODECODER";
        default:                                 return "NONE";
    }
}

// ---------------------------------------------------------------------------
// Reply formatting
// ---------------------------------------------------------------------------

void TPS25751BenchProtocol::beginLine(Stream &out, const char *id)
{
    out.print('@');
    out.print((id != nullptr && id[0] != '\0') ? id : "?");
    out.print(' ');
}

void TPS25751BenchProtocol::writeOk(Stream &out, const char *id)
{
    beginLine(out, id);
    out.println(F("OK"));
}

void TPS25751BenchProtocol::beginError(Stream &out, const char *id, TPS25751BenchError error)
{
    beginLine(out, id);
    out.print(F("ERR "));
    out.print(errorCode(error));
    out.print(' ');
}

void TPS25751BenchProtocol::writeError(Stream &out, const char *id, TPS25751BenchError error,
                                       const __FlashStringHelper *text)
{
    beginError(out, id, error);
    out.println(text);
}

void TPS25751BenchProtocol::writeEvent(Stream &out, const __FlashStringHelper *text)
{
    out.print(F("! "));
    out.println(text);
}

void TPS25751BenchProtocol::printHex(Stream &out, const uint8_t *data, size_t length)
{
    static const char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        out.print(kDigits[data[i] >> 4]);
        out.print(kDigits[data[i] & 0x0F]);
    }
}

void TPS25751BenchProtocol::printAddress(Stream &out, uint8_t address)
{
    out.print(F("0x"));
    printHex(out, &address, 1);
}

void TPS25751BenchProtocol::printTask(Stream &out, TPS25751FourCC code)
{
    for (char c : code.code) {
        out.print((c > ' ' && c <= '~') ? c : '?');
    }
}

void TPS25751BenchProtocol::printRegister(Stream &out, uint8_t address, uint8_t lengthByte,
                                          const uint8_t *data, size_t dataLength)
{
    out.print(F("reg="));
    printAddress(out, address);
    out.print(F(" len="));
    out.print(static_cast<unsigned int>(lengthByte));
    out.print(F(" data="));
    printHex(out, data, dataLength);
}

// ---------------------------------------------------------------------------
// TPS25751BenchLinePrefixer
// ---------------------------------------------------------------------------

TPS25751BenchLinePrefixer::TPS25751BenchLinePrefixer(Stream &out, const char *id)
    : _out(out), _id(id)
{
}

size_t TPS25751BenchLinePrefixer::write(uint8_t c)
{
    if (c == '\r') return 1;  // lines end with the line feed alone
    if (!_lineOpen) {
        TPS25751BenchProtocol::beginLine(_out, _id);
        _out.print('|');
        _lineOpen = true;
        if (c != '\n') _out.print(' ');
    }
    if (c == '\n') {
        _out.println();
        _lineOpen = false;
    } else {
        _out.print(static_cast<char>(c));
    }
    return 1;
}

void TPS25751BenchLinePrefixer::finish()
{
    if (_lineOpen) {
        _out.println();
        _lineOpen = false;
    }
}
