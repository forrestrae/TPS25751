#include "TPS25751BenchConsole.h"

#include <string.h>

#include "TPS25751ActivePDOContract.h"
#include "TPS25751ActiveRDOContract.h"
#include "TPS25751BootFlags.h"
#include "TPS25751InterruptEvent.h"
#include "TPS25751Mode.h"
#include "TPS25751PD3Config.h"
#include "TPS25751PatchLoader.h"
#include "TPS25751Rdo.h"
#include "TPS25751RegisterFactory.h"
#include "TPS25751Status.h"
#include "TPS25751TransmitSourceCaps.h"

// The library version is not available to the sources as a constant: the
// release tooling writes it to library.json and library.properties only. A
// build that wants HELLO to report it defines TPS25751_LIBRARY_VERSION, as
// examples/pd-bench/library_version.py does for the bench example.
#ifndef TPS25751_LIBRARY_VERSION
#define TPS25751_LIBRARY_VERSION "unknown"
#endif

namespace {

using Address = TPS25751Registers::Address;
using Protocol = TPS25751BenchProtocol;

constexpr size_t kDataSize = TPS25751Registers::Registers::DATA.size;
constexpr size_t kCommandSize = TPS25751Registers::Registers::COMMAND.size;
constexpr uint32_t kPatchReadyPollMs = 20;

// Written to DATA before a restart task. The restart returns DATA to 0
// (SLVUCR8B p.62), so a DATA register that no longer holds the marker shows
// that the controller restarted.
constexpr uint8_t kRestartMarker[4] = {0xA5, 0x5A, 0xC3, 0x3C};

const TPS25751FourCC kWarmRestart = TPS25751FourCC::of("Gaid");
const TPS25751FourCC kColdRestart = TPS25751FourCC::of("GAID");

void printMillis(Stream &out)
{
    out.print(F(" t_ms="));
    out.print(static_cast<unsigned long>(millis()));
}

// The last token of a patch report: whether the patch loader wrote anything
// to the controller. 0 means the load ended in step 1 or 2, which only read.
void printSent(Stream &out, bool sent)
{
    out.print(sent ? F(" sent=1") : F(" sent=0"));
}

// Prints text with spaces replaced by underscores, so it stays one token.
void printToken(Stream &out, const char *text)
{
    for (; *text != '\0'; ++text) out.print(*text == ' ' ? '_' : *text);
}

// The PD3_CONFIGURATION bits that TPS25751BenchProtocol::pd3EnableBit() can
// return, with the TPS25751PD3Config accessor and setter of each. All six
// "Support" bits for a data block are here, including bit 18 for 0x78, so a new
// coupling in the protocol needs no change in the console.
struct Pd3Switch {
    uint8_t bit;
    bool (TPS25751PD3Config::*isSet)() const;
    void (TPS25751PD3Config::*set)(bool);
};
const Pd3Switch kPd3Switches[] = {
    {TPS25751PD3Config::kSupportSourceExtendedBit, &TPS25751PD3Config::supportSourceExtended,
     &TPS25751PD3Config::setSupportSourceExtended},
    {TPS25751PD3Config::kSupportBatteryCapabilitiesBit,
     &TPS25751PD3Config::supportBatteryCapabilities,
     &TPS25751PD3Config::setSupportBatteryCapabilities},
    {TPS25751PD3Config::kSupportBatteryStatusBit, &TPS25751PD3Config::supportBatteryStatus,
     &TPS25751PD3Config::setSupportBatteryStatus},
    {TPS25751PD3Config::kSupportSinkCapExtendedBit, &TPS25751PD3Config::supportSinkCapExtended,
     &TPS25751PD3Config::setSupportSinkCapExtended},
    {TPS25751PD3Config::kSupportGetSourceInfoBit, &TPS25751PD3Config::supportGetSourceInfo,
     &TPS25751PD3Config::setSupportGetSourceInfo},
    {TPS25751PD3Config::kSupportPpsStatusBit, &TPS25751PD3Config::supportPpsStatus,
     &TPS25751PD3Config::setSupportPpsStatus},
};

const Pd3Switch *pd3SwitchFor(uint8_t bit)
{
    for (const Pd3Switch &entry : kPd3Switches) {
        if (entry.bit == bit) return &entry;
    }
    return nullptr;
}

const __FlashStringHelper *taskStatusText(TPS25751TaskStatus status)
{
    switch (status) {
        case TPS25751TaskStatus::Success:  return F("success");
        case TPS25751TaskStatus::Rejected: return F("rejected");
        case TPS25751TaskStatus::Timeout:  return F("timeout");
        default:                           return F("i2c_error");
    }
}

}  // namespace

TPS25751BenchConsole::TPS25751BenchConsole(TPS25751 &pd, Stream &io,
                                           const uint8_t *patchBundle, size_t patchBundleLength)
    : _pd(pd), _io(io), _patchBundle(patchBundle), _patchBundleLength(patchBundleLength)
{
}

void TPS25751BenchConsole::setPatchBundle(const uint8_t *image, size_t length)
{
    _patchBundle = image;
    _patchBundleLength = length;
}

bool TPS25751BenchConsole::canSetPd3Bit(uint8_t bit)
{
    return pd3SwitchFor(bit) != nullptr;
}

bool TPS25751BenchConsole::hasPatchBundle() const
{
    return _patchBundle != nullptr && _patchBundleLength > 0;
}

void TPS25751BenchConsole::setLogStream(Stream *log)
{
    _log = log;
}

void TPS25751BenchConsole::begin()
{
    _io.print(F("! READY proto="));
    _io.println(static_cast<unsigned int>(Protocol::kVersion));

    // begin() and RESTART are the only places that load the patch bundle.
    // HELLO only reports that one is pending.
    PatchReport report;
    ensureAppMode(report);
    if (report.attempted) {
        _io.print(F("! PATCH result="));
        _io.print(TPS25751PatchLoader::toString(report.status));
        _io.print(F(" mode="));
        _io.print(report.mode);
        printSent(_io, report.sent);
        _io.println();
    } else if (report.noBundle) {
        Protocol::writeEvent(_io, F("PATCH result=NoBundle mode=PTCH"));
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void TPS25751BenchConsole::poll()
{
    while (_io.available() > 0) {
        const int c = _io.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c != '\n') {
            // A NUL would end the C string early and hide the rest of the line
            // from the parser, so the whole line is refused.
            if (c == '\0') _lineHasNul = true;
            if (_lineLength < Protocol::kMaxLineLength) {
                _line[_lineLength++] = static_cast<char>(c);
            } else {
                _lineOverflow = true;
            }
            continue;
        }

        _line[_lineLength] = '\0';
        if (_lineOverflow) {
            rejectLine(TPS25751BenchError::TooLong, F("request line is too long"));
        } else if (_lineHasNul) {
            rejectLine(TPS25751BenchError::Syntax, F("request line contains a NUL byte"));
        } else {
            handleLine(_line);
        }
        _lineLength = 0;
        _lineOverflow = false;
        _lineHasNul = false;
    }
}

void TPS25751BenchConsole::rejectLine(TPS25751BenchError error, const __FlashStringHelper *text)
{
    const uint32_t startedMs = millis();
    _lastError = TPS25751BenchError::None;

    // The id sits at the start of the line, so it survives a cut or a NUL
    // further on. With an id the refusal is a reply; without one, an event.
    TPS25751BenchRequest request;
    Protocol::parse(_line, request);
    if (request.id[0] != '\0') {
        fail(request.id, error, text);
    } else {
        _lastError = error;
        _io.print(F("! ERR "));
        _io.print(Protocol::errorCode(error));
        _io.print(' ');
        _io.println(text);
    }
    logRequest(_line, startedMs);
}

void TPS25751BenchConsole::handleLine(const char *line)
{
    if (line == nullptr) return;
    const char *first = line;
    while (*first == ' ' || *first == '\t' || *first == '\r') ++first;
    if (*first == '\0') return;  // blank line

    const uint32_t startedMs = millis();
    _lastError = TPS25751BenchError::None;

    TPS25751BenchRequest request;
    const TPS25751BenchError error = Protocol::parse(line, request);
    if (error != TPS25751BenchError::None) {
        if (request.id[0] == '\0') {
            // Without an id there is no request to answer, so report an event.
            _lastError = error;
            Protocol::writeEvent(_io, F("ERR SYNTAX expected @<id> <COMMAND>"));
        } else if (error == TPS25751BenchError::UnknownCommand) {
            fail(request.id, error, F("unknown command"));
        } else if (error == TPS25751BenchError::BadArgument) {
            fail(request.id, error, F("missing, extra, or malformed argument"));
        } else {
            fail(request.id, error, F("expected @<id> <COMMAND>"));
        }
        logRequest(line, startedMs);
        return;
    }

    switch (request.command) {
        case TPS25751BenchCommand::Hello:    handleHello(request); break;
        case TPS25751BenchCommand::Restart:  handleRestart(request.id, request.cold); break;
        case TPS25751BenchCommand::Read:     handleRead(request); break;
        case TPS25751BenchCommand::Decode:   handleDecode(request); break;
        case TPS25751BenchCommand::Snapshot: handleSnapshot(request); break;
        case TPS25751BenchCommand::Task:     handleTask(request); break;
        case TPS25751BenchCommand::Write:    handleWrite(request); break;
        default:
            fail(request.id, TPS25751BenchError::UnknownCommand, F("unknown command"));
            break;
    }
    logRequest(line, startedMs);
}

void TPS25751BenchConsole::logRequest(const char *line, uint32_t startedMs)
{
    if (_log == nullptr) return;
    _log->print(F("[bench] "));
    _log->print(line);
    _log->print(F(" -> "));
    if (_lastError == TPS25751BenchError::None) {
        _log->print(F("OK"));
    } else {
        _log->print(F("ERR "));
        _log->print(Protocol::errorCode(_lastError));
    }
    _log->print(F(" ("));
    _log->print(static_cast<unsigned long>(millis() - startedMs));
    _log->println(F(" ms)"));
}

void TPS25751BenchConsole::finishOk(const char *id)
{
    Protocol::writeOk(_io, id);
}

void TPS25751BenchConsole::fail(const char *id, TPS25751BenchError error,
                                const __FlashStringHelper *text)
{
    _lastError = error;
    Protocol::writeError(_io, id, error, text);
}

void TPS25751BenchConsole::beginFail(const char *id, TPS25751BenchError error)
{
    _lastError = error;
    Protocol::beginError(_io, id, error);
}

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------

bool TPS25751BenchConsole::readRaw(uint8_t address, RawRegister &out) const
{
    // Ask for the size the library knows. For an address it has no size for,
    // ask for one byte to learn the length byte, then read that many: the TRM
    // lets a read stop before all N bytes (SLVUCR8B section 1.3.1, p.9), so
    // the console never clocks more bytes than the register holds.
    const uint8_t known = TPS25751Registers::getRegisterSize(static_cast<Address>(address));
    size_t capacity = (known != 0) ? known : 1;

    if (!_pd.readRegisterRaw(address, out.data, capacity, out.lengthByte)) return false;
    if (out.lengthByte > kDataSize) return false;  // no register is longer than DATA
    if (out.lengthByte > capacity) {
        capacity = out.lengthByte;
        if (!_pd.readRegisterRaw(address, out.data, capacity, out.lengthByte)) return false;
        if (out.lengthByte > kDataSize) return false;
    }
    out.dataLength = (out.lengthByte < capacity) ? out.lengthByte : static_cast<uint8_t>(capacity);
    return true;
}

TPS25751BenchConsole::ModeRead TPS25751BenchConsole::readMode(char mode[5]) const
{
    mode[0] = '?';
    mode[1] = '\0';

    RawRegister raw;
    if (!readRaw(static_cast<uint8_t>(Address::MODE), raw) ||
        raw.dataLength != TPS25751Registers::Registers::MODE.size) {
        return ModeRead::NoAnswer;
    }
    // MODE is four ASCII characters, padded with spaces: "APP ", "PTCH", "BOOT".
    char word[5];
    size_t length = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char c = static_cast<char>(raw.data[i]);
        word[i] = (c > ' ' && c <= '~') ? c : ' ';
        if (word[i] != ' ') length = i + 1;
    }
    if (length == 0) return ModeRead::NotAMode;  // all zeros or spaces: not populated yet
    word[length] = '\0';
    memcpy(mode, word, length + 1);
    return ModeRead::Mode;
}

bool TPS25751BenchConsole::readModeWithRetry(char mode[5]) const
{
    // A controller that just restarted can miss a read or return an empty MODE
    // for a moment. One failed read must not be reported as "no controller".
    for (uint8_t attempt = 0; attempt < kModeReadAttempts; ++attempt) {
        if (attempt != 0) delay(kModeReadRetryMs);
        if (readMode(mode) == ModeRead::Mode) return true;
    }
    return false;
}

bool TPS25751BenchConsole::waitForRunningMode(char mode[5], uint32_t &waitedMs, ModeRead &last) const
{
    // On hardware the controller answered COMMAND and DATA within 30 ms of its
    // warm restart and then did not give a MODE for several seconds, while the
    // port detached and attached a second time. So "COMMAND reads clear" is
    // not "ready": poll MODE, and take read errors and an empty or BOOT mode
    // as "not yet".
    const uint32_t start = millis();
    for (;;) {
        last = readMode(mode);
        waitedMs = millis() - start;
        if (last == ModeRead::Mode && (strcmp(mode, "APP") == 0 || strcmp(mode, "PTCH") == 0)) {
            return true;
        }
        if (waitedMs >= kRestartModeTimeoutMs) return false;
        delay(kRestartModePollMs);
    }
}

bool TPS25751BenchConsole::ensureAppMode(PatchReport &report)
{
    if (!readModeWithRetry(report.mode)) {
        report.i2cFailed = true;
        return false;
    }
    if (strcmp(report.mode, "APP") == 0) return true;

    // BOOT or PTCH. A controller with an EEPROM passes through both on its way
    // to APP, so one read of PTCH is no reason to load: give the controller
    // kPatchReadyTimeoutMs to reach APP. PTCH with INT_EVENT1 ReadyForPatch
    // (bit 81, SLVUCR8B p.19) ends the wait early, because it says the
    // controller waits for a patch over I2C (SLVAFV8A section 4.1, step 1).
    // The loader cannot stand in for this wait: its ReadyForPatch poll does not
    // read MODE, so a controller that reaches APP by itself would cost
    // PBM_READY_WAIT_CAP_MS and come back as CommandTimeout.
    const uint32_t start = millis();
    while (millis() - start < kPatchReadyTimeoutMs) {
        if (strcmp(report.mode, "PTCH") == 0) {
            RawRegister event;
            if (readRaw(static_cast<uint8_t>(Address::INT_EVENT1), event)) {
                const TPS25751InterruptEvent intEvent(event.data, event.dataLength);
                if (intEvent.readyForPatch()) break;
            }
        }
        delay(kPatchReadyPollMs);
        // A read that fails while the controller boots is not the last word:
        // keep the mode read before and try again.
        char now[5];
        if (readMode(now) != ModeRead::Mode) continue;
        memcpy(report.mode, now, sizeof(now));
        if (strcmp(report.mode, "APP") == 0) return true;
    }
    if (strcmp(report.mode, "PTCH") != 0) return false;

    if (!hasPatchBundle()) {
        report.noBundle = true;
        return false;
    }

    // loadPatchBundle() polls ReadyForPatch and MODE itself and sends nothing
    // until both agree, so a PTCH that outlasted the wait above without
    // ReadyForPatch ends in the loader as CommandTimeout, not as a load.
    TPS25751PatchLoader loader(_pd);
    report.attempted = true;
    report.status = loader.loadPatchBundle(_patchBundle, _patchBundleLength);
    report.sent = loader.wroteToController();
    readModeWithRetry(report.mode);
    if (strcmp(report.mode, "APP") != 0) return false;

    // APP after a load that ended in step 1 or 2 is a controller that loaded
    // its own configuration while the loader waited: the loader only read, so
    // its status is reported and does not count. The status cannot decide
    // this, because CommandTimeout is also a PBMs or PBMc that did not
    // complete. A load that wrote and then failed stays a failure, APP or not.
    return report.status == PatchLoadStatus::Success || !report.sent;
}

// ---------------------------------------------------------------------------
// HELLO
// ---------------------------------------------------------------------------

void TPS25751BenchConsole::handleHello(const TPS25751BenchRequest &request)
{
    const char *id = request.id;

    // HELLO only reads. Every read finishes before the first line starts: the
    // library can report an I2C error on its debug stream, and if that stream
    // is this one, the report must land between reply lines, never inside one.
    const unsigned long uptimeMs = millis();

    // MODE gets a few tries, so a HELLO right after a restart does not report
    // "?" for a controller that is merely still coming up.
    char mode[5];
    readModeWithRetry(mode);

    // VERSION (0x0F) is a 32-bit little-endian word (SLVUCR8B p.18).
    RawRegister raw;
    uint8_t version[4] = {0, 0, 0, 0};
    const bool haveVersion =
        readRaw(static_cast<uint8_t>(Address::VERSION), raw) && raw.dataLength == sizeof(version);
    if (haveVersion) {
        for (size_t i = 0; i < sizeof(version); ++i) version[i] = raw.data[sizeof(version) - 1 - i];
    }

    // STATUS (0x1A): Plug Present is bit 0, Port Role bit 5 (SLVUCR8B p.25).
    bool source = false;
    bool plug = false;
    const bool haveStatus = readRaw(static_cast<uint8_t>(Address::STATUS), raw);
    if (haveStatus) {
        const TPS25751Status status(raw.data, raw.dataLength);
        source = status.portRole() == PORT_SOURCE;
        plug = status.plugPresent();
    }

    // BOOT_FLAGS (0x2D) Patch Config Source, bits 31:29 (SLVUCR8B p.33).
    const __FlashStringHelper *patchSource = F("?");
    if (readRaw(static_cast<uint8_t>(Address::BOOT_FLAGS), raw)) {
        const TPS25751BootFlags bootFlags(raw.data, raw.dataLength);
        switch (bootFlags.patchConfigSource()) {
            case PatchConfigSource::NoneLoaded:   patchSource = F("none"); break;
            case PatchConfigSource::EEPROMLoaded: patchSource = F("eeprom"); break;
            case PatchConfigSource::I2CLoaded:    patchSource = F("i2c"); break;
            default:                              patchSource = F("other"); break;
        }
    }

    // A controller in PTCH mode with a bundle at hand is waiting for RESTART
    // (or a reset of the firmware, which runs begin()) to load it.
    const bool patchPending = hasPatchBundle() && strcmp(mode, "PTCH") == 0;

    Protocol::beginLine(_io, id);
    _io.print(F("proto="));
    _io.print(static_cast<unsigned int>(Protocol::kVersion));
    _io.print(F(" lib="));
    printToken(_io, TPS25751_LIBRARY_VERSION);
    _io.print(F(" build="));
    printToken(_io, __DATE__ " " __TIME__);
    _io.print(F(" uptime_ms="));
    _io.println(uptimeMs);

    Protocol::beginLine(_io, id);
    _io.print(F("mode="));
    _io.print(mode);
    _io.print(F(" version="));
    if (haveVersion) {
        Protocol::printHex(_io, version, sizeof(version));
    } else {
        _io.print('?');
    }
    if (haveStatus) {
        _io.print(F(" role="));
        _io.print(source ? F("source") : F("sink"));
        _io.print(F(" plug="));
        _io.println(plug ? '1' : '0');
    } else {
        _io.println(F(" role=? plug=?"));
    }

    Protocol::beginLine(_io, id);
    _io.print(F("patch_bundle="));
    _io.print(hasPatchBundle() ? '1' : '0');
    _io.print(F(" patch_pending="));
    _io.print(patchPending ? '1' : '0');
    _io.print(F(" patch_source="));
    _io.print(patchSource);
    _io.print(F(" swaps="));
    _io.println(TPS25751_BENCH_SWAPS_ALLOWED ? '1' : '0');

    finishOk(id);
}

// ---------------------------------------------------------------------------
// RD, DEC, SNAP, WR
// ---------------------------------------------------------------------------

void TPS25751BenchConsole::handleRead(const TPS25751BenchRequest &request)
{
    RawRegister raw;
    if (!readRaw(request.address, raw)) {
        fail(request.id, TPS25751BenchError::I2C, F("register read failed"));
        return;
    }
    Protocol::beginLine(_io, request.id);
    Protocol::printRegister(_io, request.address, raw.lengthByte, raw.data, raw.dataLength);
    _io.println();
    finishOk(request.id);
}

void TPS25751BenchConsole::handleDecode(const TPS25751BenchRequest &request)
{
    RawRegister raw;
    if (!readRaw(request.address, raw)) {
        fail(request.id, TPS25751BenchError::I2C, F("register read failed"));
        return;
    }
    Protocol::beginLine(_io, request.id);
    Protocol::printRegister(_io, request.address, raw.lengthByte, raw.data, raw.dataLength);
    _io.println();

    const Address address = static_cast<Address>(request.address);
    if (raw.dataLength == 0 || !TPS25751Factory::getInstance().createRegister(address)) {
        fail(request.id, TPS25751BenchError::NoDecoder,
             F("the library has no register class for this address"));
        return;
    }
    printDecode(request.id, request.address, raw);
    finishOk(request.id);
}

void TPS25751BenchConsole::printDecode(const char *id, uint8_t address, const RawRegister &raw)
{
    TPS25751BenchLinePrefixer lines(_io, id);

    if (static_cast<Address>(address) == Address::ACTIVE_RDO_CONTRACT) {
        // An RDO's layout follows the PDO it requests, which is not in 0x35
        // (USB PD R3.3 Tables 6.19 to 6.22). The own RDO takes its kind from
        // the active PDO in 0x34. The requested RDO is a partner's request only
        // while this port is Source, and takes its kind from the PDO this
        // device advertises in 0x32 at the RDO's Object Position.
        const TPS25751ActiveRDOContract rdoContract(raw.data, raw.dataLength);
        RdoKind ownKind = RdoKind::Unknown;
        RdoKind requestedKind = RdoKind::Unknown;

        RawRegister other;
        if (readRaw(static_cast<uint8_t>(Address::ACTIVE_PDO_CONTRACT), other)) {
            const TPS25751ActivePDOContract pdoContract(other.data, other.dataLength);
            ownKind = rdoKindFor(pdoContract.activePdo());
        }
        const uint8_t position = rdoContract.requestedRdo().objectPosition();
        if (rdoContract.rawRequestedRdo() != 0 && position >= 1 && position <= 7 &&
            readRaw(static_cast<uint8_t>(Address::STATUS), other)) {
            const TPS25751Status status(other.data, other.dataLength);
            if (status.portRole() == PORT_SOURCE &&
                readRaw(static_cast<uint8_t>(Address::TRANSMIT_SOURCE_CAPABILITIES), other)) {
                const TPS25751TransmitSourceCaps sourceCaps(other.data, other.dataLength);
                requestedKind = rdoKindFor(sourceCaps.pdo(position - 1));
            }
        }
        rdoContract.debugPrint(lines, ownKind, requestedKind);
    } else {
        const auto reg = TPS25751Factory::getInstance().createRegister(
            static_cast<Address>(address), raw.data, raw.dataLength);
        if (reg) reg->debugPrint(lines);
    }
    lines.finish();
}

void TPS25751BenchConsole::handleSnapshot(const TPS25751BenchRequest &request)
{
    const char *id = request.id;

    Protocol::beginLine(_io, id);
    _io.print(F("uptime_ms="));
    _io.println(static_cast<unsigned long>(millis()));

    unsigned int failed = 0;
    bool haveEvent = false;
    RawRegister event;

    for (uint8_t address : Protocol::kSnapshotRegisters) {
        // Read first, then print the whole line: an I2C error the library
        // reports on its debug stream must not land inside a reply line.
        RawRegister raw;
        const bool answered = readRaw(address, raw);

        Protocol::beginLine(_io, id);
        if (answered) {
            Protocol::printRegister(_io, address, raw.lengthByte, raw.data, raw.dataLength);
            if (static_cast<Address>(address) == Address::INT_EVENT1) {
                event = raw;
                haveEvent = true;
            }
        } else {
            ++failed;
            _io.print(F("reg="));
            Protocol::printAddress(_io, address);
            _io.print(F(" error=I2C"));
        }
        _io.println();
    }

    // Clear exactly the INT_EVENT1 bits this snapshot reported: a 1 written to
    // INT_CLEAR1 clears that event (SLVUCR8B p.23). An event that arrives
    // after the read stays set for the next snapshot. The write comes before
    // its line, for the same reason as the reads.
    const __FlashStringHelper *cleared = F("skipped");
    if (haveEvent && event.dataLength == TPS25751Registers::Registers::INT_CLEAR1.size) {
        cleared = _pd.writeRegister(Address::INT_CLEAR1, event.data, event.dataLength) ? F("ok")
                                                                                       : F("failed");
    }
    Protocol::beginLine(_io, id);
    _io.print(F("int_clear="));
    _io.println(cleared);

    if (failed != 0) {
        beginFail(id, TPS25751BenchError::I2C);
        _io.print(failed);
        _io.print(F(" of "));
        _io.print(static_cast<unsigned int>(Protocol::kSnapshotRegisterCount));
        _io.println(F(" registers did not answer"));
        return;
    }
    finishOk(id);
}

void TPS25751BenchConsole::handleWrite(const TPS25751BenchRequest &request)
{
    const char *id = request.id;
    if (!Protocol::isWriteAllowed(request.address)) {
        fail(id, TPS25751BenchError::Refused, Protocol::writeRefusalText());
        return;
    }

    // The payload must be the whole register: the length byte the controller
    // reports decides how many bytes that is.
    RawRegister current;
    if (!readRaw(request.address, current)) {
        fail(id, TPS25751BenchError::I2C, F("register read failed before the write"));
        return;
    }
    if (request.payloadLength != current.lengthByte) {
        beginFail(id, TPS25751BenchError::BadArgument);
        _io.print(F("the register is "));
        _io.print(static_cast<unsigned int>(current.lengthByte));
        _io.print(F(" bytes, got "));
        _io.println(static_cast<unsigned int>(request.payloadLength));
        return;
    }

    if (!_pd.writeRegister(static_cast<Address>(request.address), request.payload,
                           request.payloadLength)) {
        fail(id, TPS25751BenchError::I2C, F("register write failed"));
        return;
    }

    RawRegister readback;
    if (!readRaw(request.address, readback)) {
        fail(id, TPS25751BenchError::I2C, F("register read failed after the write"));
        return;
    }
    const bool match = readback.dataLength == request.payloadLength &&
                       memcmp(readback.data, request.payload, request.payloadLength) == 0;

    // A PD 3.x data block is sent only while its PD3_CONFIGURATION bit is set
    // (SLVUCR8B p.46), so set that bit from a fresh read of 0x42 and touch
    // nothing else. Every transaction comes before the first reply line, so a
    // message on the library's debug stream cannot land inside one.
    enum class Pd3Step : uint8_t { NotCoupled, NoSwitch, AlreadySet, Written, ReadFailed,
                                   WrongLength, WriteFailed, ReadbackFailed };
    Pd3Step pd3Step = Pd3Step::NotCoupled;
    RawRegister pd3Readback;
    bool pd3Match = false;

    const uint8_t pd3Bit = Protocol::pd3EnableBit(request.address);
    const Pd3Switch *pd3Switch = pd3SwitchFor(pd3Bit);
    constexpr size_t kPd3Size = TPS25751Registers::Registers::PD3_CONFIGURATION.size;
    RawRegister pd3Current;
    if (pd3Bit != Protocol::kNoPd3EnableBit && pd3Switch == nullptr) {
        // The protocol couples this register to a bit kPd3Switches does not
        // list: refuse to report success for a reply that was never enabled.
        pd3Step = Pd3Step::NoSwitch;
    } else if (pd3Switch != nullptr) {
        if (!readRaw(static_cast<uint8_t>(Address::PD3_CONFIGURATION), pd3Current)) {
            pd3Step = Pd3Step::ReadFailed;
        } else if (pd3Current.dataLength != kPd3Size) {
            pd3Step = Pd3Step::WrongLength;
        } else {
            TPS25751PD3Config pd3(pd3Current.data, pd3Current.dataLength);
            if ((pd3.*(pd3Switch->isSet))()) {
                pd3Step = Pd3Step::AlreadySet;
            } else {
                (pd3.*(pd3Switch->set))(true);
                if (!_pd.writeRegister(Address::PD3_CONFIGURATION, pd3.raw(), pd3.size())) {
                    pd3Step = Pd3Step::WriteFailed;
                } else if (!readRaw(static_cast<uint8_t>(Address::PD3_CONFIGURATION),
                                    pd3Readback)) {
                    pd3Step = Pd3Step::ReadbackFailed;
                } else {
                    pd3Step = Pd3Step::Written;
                    pd3Match = pd3Readback.dataLength == pd3.size() &&
                               memcmp(pd3Readback.data, pd3.raw(), pd3.size()) == 0;
                }
            }
        }
    }

    Protocol::beginLine(_io, id);
    Protocol::printRegister(_io, request.address, readback.lengthByte, readback.data,
                            readback.dataLength);
    _io.print(F(" match="));
    _io.println(match ? '1' : '0');

    switch (pd3Step) {
        case Pd3Step::NotCoupled:
            break;
        case Pd3Step::NoSwitch:
            beginFail(id, TPS25751BenchError::I2C);
            _io.print(F("the data register was written; the 0x42 enable bit was not set: "
                        "the console has no setter for bit "));
            _io.println(static_cast<unsigned int>(pd3Bit));
            return;
        case Pd3Step::WrongLength:
            beginFail(id, TPS25751BenchError::I2C);
            _io.print(F("the data register was written; the 0x42 enable bit was not set: "
                        "PD3_CONFIGURATION is "));
            _io.print(static_cast<unsigned int>(pd3Current.dataLength));
            _io.print(F(" bytes, expected "));
            _io.println(static_cast<unsigned int>(kPd3Size));
            return;
        case Pd3Step::AlreadySet:
            Protocol::beginLine(_io, id);
            _io.println(F("pd3=already"));
            break;
        case Pd3Step::Written:
            Protocol::beginLine(_io, id);
            Protocol::printRegister(_io, static_cast<uint8_t>(Address::PD3_CONFIGURATION),
                                    pd3Readback.lengthByte, pd3Readback.data,
                                    pd3Readback.dataLength);
            _io.print(F(" match="));
            _io.println(pd3Match ? '1' : '0');
            break;
        case Pd3Step::ReadFailed:
            fail(id, TPS25751BenchError::I2C,
                 F("the data register was written; the 0x42 enable bit was not set: "
                   "PD3_CONFIGURATION read failed"));
            return;
        case Pd3Step::WriteFailed:
            fail(id, TPS25751BenchError::I2C,
                 F("the data register was written; the 0x42 enable bit was not set: "
                   "PD3_CONFIGURATION write failed"));
            return;
        case Pd3Step::ReadbackFailed:
            fail(id, TPS25751BenchError::I2C,
                 F("the data register was written; the 0x42 enable bit was not confirmed: "
                   "PD3_CONFIGURATION read failed after its write"));
            return;
    }
    finishOk(id);
}

// ---------------------------------------------------------------------------
// CMD
// ---------------------------------------------------------------------------

void TPS25751BenchConsole::handleTask(const TPS25751BenchRequest &request)
{
    const char *id = request.id;
    const TPS25751BenchTaskPolicy policy = Protocol::taskPolicy(request.task);

    if (policy == TPS25751BenchTaskPolicy::Restart) {
        handleRestart(id, Protocol::sameTask(request.task, kColdRestart));
        return;
    }
    if (policy != TPS25751BenchTaskPolicy::Allowed) {
        fail(id, TPS25751BenchError::Refused, Protocol::refusalText(policy));
        return;
    }

    uint8_t data[kDataSize] = {0};
    const uint32_t timeoutMs = Protocol::taskTimeoutMs(request.task);
    const TPS25751TaskResult result = _pd.executeCommand(
        request.task,
        request.payloadLength > 0 ? request.payload : nullptr, request.payloadLength,
        data, sizeof(data), timeoutMs);

    switch (result.status) {
        case TPS25751TaskStatus::Success:
            break;
        case TPS25751TaskStatus::Rejected:
            fail(id, TPS25751BenchError::Rejected, F("the controller answered !CMD"));
            return;
        case TPS25751TaskStatus::Timeout:
            beginFail(id, TPS25751BenchError::Timeout);
            _io.print(F("COMMAND did not clear within "));
            _io.print(static_cast<unsigned long>(timeoutMs));
            _io.println(F(" ms"));
            return;
        default:
            fail(id, TPS25751BenchError::I2C, F("the controller did not answer during the task"));
            return;
    }

    // DATA byte 1 is the Standard Task Return Code for most tasks; bits 3:0
    // are 0 for success, 1 for timed out or aborted, 3 for rejected, and 4 for
    // a locked Rx buffer (SLVUCR8B Table 4-1, p.61).
    Protocol::beginLine(_io, id);
    _io.print(F("task="));
    Protocol::printTask(_io, request.task);
    _io.print(F(" status=success rc="));
    Protocol::printAddress(_io, result.returnCode);
    _io.println();

    Protocol::beginLine(_io, id);
    _io.print(F("data="));
    Protocol::printHex(_io, data, sizeof(data));
    _io.println();
    finishOk(id);
}

// ---------------------------------------------------------------------------
// RESTART
// ---------------------------------------------------------------------------

void TPS25751BenchConsole::handleRestart(const char *id, bool cold)
{
    const TPS25751FourCC task = cold ? kColdRestart : kWarmRestart;
    RawRegister raw;

    // 'GAID' reboots through the OTP bootloader (SLVUCR8B p.62). A controller
    // that took its configuration from an EEPROM loads it again; one that was
    // patched over I2C comes back in PTCH mode and needs the bundle.
    if (cold && !hasPatchBundle()) {
        if (!readRaw(static_cast<uint8_t>(Address::BOOT_FLAGS), raw)) {
            fail(id, TPS25751BenchError::I2C, F("BOOT_FLAGS read failed"));
            return;
        }
        const TPS25751BootFlags bootFlags(raw.data, raw.dataLength);
        if (bootFlags.patchConfigSource() != PatchConfigSource::EEPROMLoaded) {
            fail(id, TPS25751BenchError::NoBundle,
                 F("the configuration did not come from an EEPROM and no patch bundle is compiled in"));
            return;
        }
    }

    // Mark DATA, then issue the task. Neither task takes input, and "all
    // CMDx/DATAx registers will return to 0" on the reboot (SLVUCR8B p.62).
    uint8_t marker[kDataSize] = {0};
    memcpy(marker, kRestartMarker, sizeof(kRestartMarker));
    if (!_pd.writeRegister(Address::DATA, marker, sizeof(marker))) {
        fail(id, TPS25751BenchError::I2C, F("DATA write failed before the restart"));
        return;
    }

    const TPS25751TaskResult issued =
        _pd.executeCommand(task, nullptr, 0, nullptr, 0, kRestartIssueTimeoutMs);
    if (issued.status == TPS25751TaskStatus::Rejected) {
        fail(id, TPS25751BenchError::Rejected, F("the controller answered !CMD"));
        return;
    }

    Protocol::beginLine(_io, id);
    _io.print(F("step=issue task="));
    Protocol::printTask(_io, task);
    _io.print(F(" status="));
    _io.print(taskStatusText(issued.status));
    printMillis(_io);
    _io.println();

    // Wait until the controller answers with COMMAND clear and shows that it
    // restarted. Three observations count: DATA lost the marker, the
    // controller stopped answering for a while ("can momentarily NAK I2C
    // transactions while rebooting", p.62), or COMMAND held the task code and
    // then cleared. A COMMAND that reads clear with none of the three means
    // the task never reached the controller.
    bool sawPending = (issued.status == TPS25751TaskStatus::Timeout);
    bool sawNak = false;
    bool restarted = false;
    const __FlashStringHelper *evidence = F("none");
    const uint32_t start = millis();

    // executeCommand() reports an I2C error both when the COMMAND write was not
    // acknowledged and when the write went through and the controller, already
    // rebooting, did not answer the poll. The first poll below tells them
    // apart, so the first case does not wait out the whole timeout.
    bool issueInDoubt = (issued.status == TPS25751TaskStatus::I2CError);

    while (millis() - start < kRestartTimeoutMs) {
        if (!issueInDoubt) delay(kRestartPollMs);

        uint8_t command[kCommandSize];
        uint8_t length = 0;
        if (!_pd.readRegisterRaw(static_cast<uint8_t>(Address::COMMAND), command,
                                 sizeof(command), length) ||
            length != kCommandSize) {
            issueInDoubt = false;  // a silent controller is what a reboot looks like
            if (!sawNak) {
                sawNak = true;
                Protocol::beginLine(_io, id);
                _io.print(F("step=nak"));
                printMillis(_io);
                _io.println();
            }
            continue;
        }
        if (memcmp(command, task.code, sizeof(command)) == 0) {
            issueInDoubt = false;
            sawPending = true;  // still in Type-C Error Recovery
            continue;
        }
        if (memcmp(command, TPS25751_CMD_REJECTED.code, sizeof(command)) == 0) {
            // The controller took longer than the issue window to refuse.
            fail(id, TPS25751BenchError::Rejected,
                 F("the controller answered !CMD while the restart was pending"));
            return;
        }
        if (memcmp(command, TPS25751_CMD_CLEAR.code, sizeof(command)) != 0) {
            issueInDoubt = false;
            continue;
        }

        const bool dataRead = readRaw(static_cast<uint8_t>(Address::DATA), raw) &&
                              raw.dataLength >= sizeof(kRestartMarker);
        const bool markerGone =
            dataRead && memcmp(raw.data, kRestartMarker, sizeof(kRestartMarker)) != 0;
        if (issueInDoubt && dataRead && !markerGone) {
            // The controller answers, holds no task, and DATA still has the
            // marker: the COMMAND write is what failed.
            fail(id, TPS25751BenchError::I2C,
                 F("the COMMAND write failed and the task was not issued"));
            return;
        }
        issueInDoubt = false;
        if (markerGone) {
            evidence = F("data_cleared");
        } else if (sawNak) {
            evidence = F("nak");
        } else if (sawPending) {
            evidence = F("command_cleared");
        } else {
            continue;
        }
        restarted = true;
        break;
    }

    if (!restarted) {
        beginFail(id, sawNak ? TPS25751BenchError::I2C : TPS25751BenchError::Timeout);
        _io.print(sawNak ? F("the controller did not answer again within ")
                         : F("no restart observed within "));
        _io.print(static_cast<unsigned long>(kRestartTimeoutMs));
        _io.println(F(" ms"));
        return;
    }

    Protocol::beginLine(_io, id);
    _io.print(F("step=back evidence="));
    _io.print(evidence);
    printMillis(_io);
    _io.println();

    // The controller answers again, but that does not make it ready: wait for
    // MODE to read APP, or PTCH for the patch path.
    char mode[5];
    uint32_t waitedMs = 0;
    ModeRead lastRead = ModeRead::NoAnswer;
    if (!waitForRunningMode(mode, waitedMs, lastRead)) {
        beginFail(id, TPS25751BenchError::Timeout);
        _io.print(F("MODE did not read APP or PTCH within "));
        _io.print(static_cast<unsigned long>(kRestartModeTimeoutMs));
        _io.print(F(" ms after the restart; last read: "));
        if (lastRead == ModeRead::NoAnswer) {
            _io.println(F("no answer"));
        } else if (lastRead == ModeRead::NotAMode) {
            _io.println(F("an empty MODE"));
        } else {
            _io.println(mode);
        }
        return;
    }
    Protocol::beginLine(_io, id);
    _io.print(F("step=mode"));
    printMillis(_io);
    _io.print(F(" mode="));
    _io.print(mode);
    _io.print(F(" waited_ms="));
    _io.println(static_cast<unsigned long>(waitedMs));

    PatchReport report;
    const bool inApp = ensureAppMode(report);
    if (report.attempted) {
        Protocol::beginLine(_io, id);
        _io.print(F("step=patch result="));
        _io.print(TPS25751PatchLoader::toString(report.status));
        printMillis(_io);
        printSent(_io, report.sent);
        _io.println();
    }
    if (!inApp) {
        if (report.i2cFailed) {
            fail(id, TPS25751BenchError::I2C, F("MODE read failed after the restart"));
        } else if (report.noBundle) {
            fail(id, TPS25751BenchError::NoBundle,
                 F("the controller is in PTCH mode and no patch bundle is compiled in"));
        } else if (report.attempted) {
            beginFail(id, TPS25751BenchError::Patch);
            _io.print(F("patch load ended with "));
            _io.print(TPS25751PatchLoader::toString(report.status));
            _io.print(F(", MODE "));
            _io.println(report.mode);
        } else {
            beginFail(id, TPS25751BenchError::Timeout);
            _io.print(F("MODE stayed "));
            _io.println(report.mode);
        }
        return;
    }

    Protocol::beginLine(_io, id);
    _io.print(F("step=done mode="));
    _io.print(report.mode);
    printMillis(_io);
    _io.println();
    finishOk(id);
}
