#include "TPS25751BenchConsole.h"

#include <string.h>

#include "TPS25751ActivePDOContract.h"
#include "TPS25751ActiveRDOContract.h"
#include "TPS25751BootFlags.h"
#include "TPS25751InterruptEvent.h"
#include "TPS25751Mode.h"
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

// Prints text with spaces replaced by underscores, so it stays one token.
void printToken(Stream &out, const char *text)
{
    for (; *text != '\0'; ++text) out.print(*text == ' ' ? '_' : *text);
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

    PatchReport report;
    ensureAppMode(report);
    if (report.attempted) {
        _io.print(F("! PATCH result="));
        _io.print(TPS25751PatchLoader::toString(report.status));
        _io.print(F(" mode="));
        _io.println(report.mode);
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
            if (_lineLength < Protocol::kMaxLineLength) {
                _line[_lineLength++] = static_cast<char>(c);
            } else {
                _lineOverflow = true;
            }
            continue;
        }

        _line[_lineLength] = '\0';
        if (_lineOverflow) {
            // The id sits at the start of the line, so it survives the cut.
            TPS25751BenchRequest request;
            Protocol::parse(_line, request);
            if (request.id[0] != '\0') {
                Protocol::writeError(_io, request.id, TPS25751BenchError::TooLong,
                                     F("request line is too long"));
            } else {
                Protocol::writeEvent(_io, F("ERR TOOLONG request line is too long"));
            }
        } else {
            handleLine(_line);
        }
        _lineLength = 0;
        _lineOverflow = false;
    }
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

bool TPS25751BenchConsole::readMode(char mode[5]) const
{
    RawRegister raw;
    if (!readRaw(static_cast<uint8_t>(Address::MODE), raw) ||
        raw.dataLength != TPS25751Registers::Registers::MODE.size) {
        mode[0] = '?';
        mode[1] = '\0';
        return false;
    }
    // MODE is four ASCII characters, padded with spaces: "APP ", "PTCH", "BOOT".
    size_t length = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char c = static_cast<char>(raw.data[i]);
        mode[i] = (c > ' ' && c <= '~') ? c : ' ';
        if (mode[i] != ' ') length = i + 1;
    }
    mode[length] = '\0';
    if (length == 0) {
        mode[0] = '?';
        mode[1] = '\0';
    }
    return true;
}

bool TPS25751BenchConsole::ensureAppMode(PatchReport &report)
{
    if (!readMode(report.mode)) {
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
        if (!readMode(report.mode)) {
            report.i2cFailed = true;
            return false;
        }
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
    readMode(report.mode);
    return report.status == PatchLoadStatus::Success && strcmp(report.mode, "APP") == 0;
}

// ---------------------------------------------------------------------------
// HELLO
// ---------------------------------------------------------------------------

void TPS25751BenchConsole::handleHello(const TPS25751BenchRequest &request)
{
    const char *id = request.id;

    // The first line needs no I2C, so a host probing for the console gets it
    // at once, before a possible patch reload.
    Protocol::beginLine(_io, id);
    _io.print(F("proto="));
    _io.print(static_cast<unsigned int>(Protocol::kVersion));
    _io.print(F(" lib="));
    printToken(_io, TPS25751_LIBRARY_VERSION);
    _io.print(F(" build="));
    printToken(_io, __DATE__ " " __TIME__);
    _io.print(F(" uptime_ms="));
    _io.println(static_cast<unsigned long>(millis()));

    PatchReport report;
    ensureAppMode(report);
    if (report.attempted) {
        Protocol::beginLine(_io, id);
        _io.print(F("patch_reload="));
        _io.println(TPS25751PatchLoader::toString(report.status));
    }

    Protocol::beginLine(_io, id);
    _io.print(F("mode="));
    _io.print(report.mode);

    // VERSION (0x0F) is a 32-bit little-endian word (SLVUCR8B p.18).
    _io.print(F(" version="));
    RawRegister raw;
    if (readRaw(static_cast<uint8_t>(Address::VERSION), raw) && raw.dataLength == 4) {
        const uint8_t word[4] = {raw.data[3], raw.data[2], raw.data[1], raw.data[0]};
        Protocol::printHex(_io, word, sizeof(word));
    } else {
        _io.print('?');
    }

    // STATUS (0x1A): Plug Present is bit 0, Port Role bit 5 (SLVUCR8B p.25).
    if (readRaw(static_cast<uint8_t>(Address::STATUS), raw)) {
        const TPS25751Status status(raw.data, raw.dataLength);
        _io.print(F(" role="));
        _io.print(status.portRole() == PORT_SOURCE ? F("source") : F("sink"));
        _io.print(F(" plug="));
        _io.print(status.plugPresent() ? '1' : '0');
    } else {
        _io.print(F(" role=? plug=?"));
    }
    _io.println();

    Protocol::beginLine(_io, id);
    _io.print(F("patch_bundle="));
    _io.print(hasPatchBundle() ? '1' : '0');
    // BOOT_FLAGS (0x2D) Patch Config Source, bits 31:29 (SLVUCR8B p.33).
    _io.print(F(" patch_source="));
    if (readRaw(static_cast<uint8_t>(Address::BOOT_FLAGS), raw)) {
        const TPS25751BootFlags bootFlags(raw.data, raw.dataLength);
        switch (bootFlags.patchConfigSource()) {
            case PatchConfigSource::NoneLoaded:   _io.print(F("none")); break;
            case PatchConfigSource::EEPROMLoaded: _io.print(F("eeprom")); break;
            case PatchConfigSource::I2CLoaded:    _io.print(F("i2c")); break;
            default:                              _io.print(F("other")); break;
        }
    } else {
        _io.print('?');
    }
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
        RawRegister raw;
        Protocol::beginLine(_io, id);
        if (readRaw(address, raw)) {
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
    // after the read stays set for the next snapshot.
    Protocol::beginLine(_io, id);
    _io.print(F("int_clear="));
    if (!haveEvent || event.dataLength != TPS25751Registers::Registers::INT_CLEAR1.size) {
        _io.println(F("skipped"));
    } else if (_pd.writeRegister(Address::INT_CLEAR1, event.data, event.dataLength)) {
        _io.println(F("ok"));
    } else {
        _io.println(F("failed"));
    }

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
        fail(id, TPS25751BenchError::Refused,
             F("register is not on the write allowlist (0x27, 0x28, 0x37, 0x42)"));
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

    Protocol::beginLine(_io, id);
    Protocol::printRegister(_io, request.address, readback.lengthByte, readback.data,
                            readback.dataLength);
    _io.print(F(" match="));
    _io.println(match ? '1' : '0');
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

    while (millis() - start < kRestartTimeoutMs) {
        delay(kRestartPollMs);

        uint8_t command[kCommandSize];
        uint8_t length = 0;
        if (!_pd.readRegisterRaw(static_cast<uint8_t>(Address::COMMAND), command,
                                 sizeof(command), length) ||
            length != kCommandSize) {
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
            sawPending = true;  // still in Type-C Error Recovery
            continue;
        }
        if (memcmp(command, TPS25751_CMD_CLEAR.code, sizeof(command)) != 0) continue;

        const bool markerGone = readRaw(static_cast<uint8_t>(Address::DATA), raw) &&
                                raw.dataLength >= sizeof(kRestartMarker) &&
                                memcmp(raw.data, kRestartMarker, sizeof(kRestartMarker)) != 0;
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

    PatchReport report;
    const bool inApp = ensureAppMode(report);
    if (report.attempted) {
        Protocol::beginLine(_io, id);
        _io.print(F("step=patch result="));
        _io.print(TPS25751PatchLoader::toString(report.status));
        printMillis(_io);
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
