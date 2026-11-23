#pragma once

// Scripted TPS25751CommandExecutor for the native suites.
//
// Models what the flash 4CC tasks touch, closely enough to test a sequencer
// without hardware. The model mixes behaviour the TI documents state with
// choices the fake has to make where they are silent. Each item below says
// which it is; "assumption" means the TRM (SLVUCR8B pages 61, 62, 81, 82) does
// not describe it, and a test that relies on it pins the fake, not the device.
//
//   - A byte-array EEPROM, erased to 0xFF, of a configurable size. There is no
//     page-size model: a write is never wrapped within an EEPROM page
//     (assumption; the 24LC256 itself wraps at 64-byte pages).
//   - The 64-byte DATA register: the input payload is written to DATA bytes
//     0..inLen-1, the task writes its output, and outData is copied from DATA
//     byte 0, as TPS25751::executeCommand() does. returnCode is always DATA[0].
//   - Addresses are decoded from the 4 input bytes least significant byte
//     first. Table 4-20 states this for FLad only. Hardware stage 1 verified it
//     for FLrd on 2026-11-22; for FLvy the same stage supports it indirectly
//     (both bundles reported valid at 0x0800 and 0x4400).
//   - FLrd: 16 bytes starting at the address, DATA byte 0 holding the byte at
//     the address (Table 4-19 gives 16 bytes and no return-code byte; the byte
//     order was verified in hardware stage 1). Bytes past the end of the EEPROM
//     read 0xFF (assumption, not in the TRM). FLrd leaves the FLad write
//     address alone (assumption, not in the TRM, which does not say whether a
//     read disturbs it). Setting flrdMovesWriteAddress models the other case, a
//     hypothetical device whose FLrd leaves the write address at the address it
//     read plus 16; it is off by default.
//   - FLad: loads the write-address register and returns 0x00 (Table 4-20 and
//     the standard return codes of Table 4-1). An address at or past the end of
//     the EEPROM returns 0x03 (assumption, not in the TRM).
//   - FLwd: writes the inLen input bytes at the write address, auto-increments
//     the address, and returns 0x00 (Table 4-21). Writing exactly inLen bytes
//     is not documented; hardware stage 2 verified it for a 4-byte write on the
//     test board (2026-11-23), and for other lengths it remains a model.
//     Returning 0x03 when no FLad was issued since the last reset, or when the
//     write would pass the end, is an assumption, not in the TRM: Table 4-21
//     documents only 0x00 (written) and 0xFF (busy). 0xFF is produced only by an injected fault.
//     The write address is whatever the last FLad or FLwd left behind. When a
//     fault makes an FLad report success without taking effect, the next FLwd
//     therefore writes at the stale address, straight after the bytes written
//     before (assumption, not in the TRM, which does not say what the
//     controller does when FLad fails or does not take).
//   - FLvy: 0x00 valid, 0x01 not valid (Table 4-22). Deciding validity from the
//     four Header_ID bytes alone (little-endian 0xACE00001) is an assumption,
//     not in the TRM, which says only that the task completes after the header
//     is checked.
//   - GAID: counts the reset and makes MODE and BOOT_FLAGS reads fail for
//     rebootDurationMs of fake time, after which MODE reads modeAfterReset (the
//     controller can NAK while rebooting, Table 4-3). It clears DATA, as the TRM
//     says all CMDx and DATAx registers return to 0. Clearing the FLad write
//     address is an assumption, not in the TRM. So is the timing: the reboot
//     starts at once and the task completes at once, where Table 4-3 describes
//     a typical 1-second Type-C Error Recovery delay before the restart and
//     does not say what COMMAND and MODE read during it.
//   - Any other code: COMMAND reads "!CMD" (Rejected).
//   - MODE and BOOT_FLAGS register contents, with read failures on demand.
//   - A fake clock: delayMs() advances it, and every executeCommand() advances
//     it by msPerCommand. It wraps like millis(), and the reboot window is
//     measured by unsigned subtraction, so a test can start it near the wrap.
//   - A log of every executeCommand() call (4CC, input payload, outLen,
//     timeout, result).
//
// Faults are injected per call, either by the global call index or by the n-th
// call of one task code. A fault can force a non-Success executor status, force
// a return code, suppress or keep the task's effect, and run a hook that edits
// the fake (for example to change bytes next to a pointer during an FLwd).
//
// Header-only and host-only: it uses std::vector, std::string, and
// std::function, which are fine on the host and never reach the target.

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "TPS25751CommandExecutor.h"
#include "TPS25751Task.h"
#include "TPS25751Mode.h"
#include "TPS25751BootFlags.h"
#include "TPS25751RegisterAddress.h"

class FakeCommandExecutor : public TPS25751CommandExecutor
{
public:
    static constexpr size_t kDataSize = TPS25751Registers::Registers::DATA.size;  // 64
    static constexpr size_t kModeSize = TPS25751Registers::Registers::MODE.size;  // 4
    static constexpr size_t kBootSize = TPS25751Registers::Registers::BOOT_FLAGS.size;  // 5

    static constexpr uint8_t kRcSuccess = 0x00;   // standard "completed", TRM Table 4-1
    static constexpr uint8_t kRcRejected = 0x03;  // standard "rejected", Table 4-1; see the
                                                  // assumptions above for where the fake uses it
    static constexpr uint8_t kRcBusy = 0xFF;      // FLwd "flash is busy", Table 4-21
    static constexpr uint8_t kRcNotValid = 0x01;  // FLvy "not valid", Table 4-22
    static constexpr uint32_t kHeaderId = 0xACE00001UL;

    // BOOT_FLAGS (0x2D) bit positions, TRM SLVUCR8B p.33.
    static constexpr uint8_t kBitI2cEepromPresent = 3;
    static constexpr uint8_t kBitRegion0Invalid = 6;
    static constexpr uint8_t kBitRegion1Invalid = 7;
    static constexpr uint8_t kBitRegion0EepromError = 8;
    static constexpr uint8_t kBitRegion1EepromError = 9;

    // An injected fault. Defaults describe "no change"; use the factories.
    struct Fault
    {
        TPS25751TaskStatus status = TPS25751TaskStatus::Success;
        int returnCode = -1;   // 0..255 overrides DATA[0] after the task
        bool runTask = false;  // run the task's effect even though a fault applies
        std::function<void(FakeCommandExecutor &)> hook;  // runs after the task

        static Fault rejected() { Fault f; f.status = TPS25751TaskStatus::Rejected; return f; }
        static Fault timeout() { Fault f; f.status = TPS25751TaskStatus::Timeout; return f; }
        static Fault i2cError() { Fault f; f.status = TPS25751TaskStatus::I2CError; return f; }
        // Completes, but DATA[0] reads rc and the task has no effect.
        static Fault returnCodeOnly(uint8_t rc) { Fault f; f.returnCode = rc; return f; }
        static Fault busy() { return returnCodeOnly(kRcBusy); }
        // Runs the task normally, then the hook.
        static Fault withHook(std::function<void(FakeCommandExecutor &)> h)
        {
            Fault f;
            f.runTask = true;
            f.hook = std::move(h);
            return f;
        }
    };

    struct Call
    {
        std::string cmd;  // 4 characters
        std::vector<uint8_t> in;
        size_t outLen;
        uint32_t timeoutMs;
        TPS25751TaskResult result;
    };

    explicit FakeCommandExecutor(size_t eepromSize = 32768)
        : eeprom(eepromSize, 0xFF)
    {
        setMode("APP ");
        setModeAfterReset("APP ");
        memset(data, 0, sizeof(data));
        memset(bootFlags, 0, sizeof(bootFlags));
    }

    // --- State (public so tests can inspect and edit it directly) -----------

    std::vector<uint8_t> eeprom;
    uint8_t data[kDataSize];
    uint8_t mode[kModeSize];
    uint8_t modeAfterReset[kModeSize];
    uint8_t bootFlags[kBootSize];

    uint32_t writeAddress = 0;
    bool writeAddressSet = false;

    // Hypothetical device behaviour, off by default: every FLrd leaves the write
    // address at the address it read plus 16. Not in the TRM, which is silent on
    // whether a read moves the write address; the default is that it does not.
    bool flrdMovesWriteAddress = false;

    uint32_t now = 0;
    uint32_t msPerCommand = 0;
    std::vector<uint32_t> delays;

    bool modeReadFails = false;
    bool bootFlagsReadFails = false;
    size_t modeReads = 0;
    size_t bootFlagsReads = 0;

    uint32_t rebootDurationMs = 1000;
    bool rebooting = false;
    uint32_t rebootStartedAt = 0;
    size_t resetCount = 0;

    std::vector<Call> calls;

    // When false, the log keeps each call's code, sizes, timeout, and result but
    // not its input bytes. A sweep that runs thousands of updates and looks
    // only at the number of calls turns this off to save an allocation per call.
    bool logPayloads = true;

    // --- Setup helpers -------------------------------------------------------

    void setMode(const char (&m)[5]) { memcpy(mode, m, kModeSize); }
    void setModeAfterReset(const char (&m)[5]) { memcpy(modeAfterReset, m, kModeSize); }

    void setBootBit(uint8_t bit, bool value)
    {
        const uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
        if (value) bootFlags[bit / 8] |= mask;
        else bootFlags[bit / 8] &= static_cast<uint8_t>(~mask);
    }

    // Patch Config Source, BOOT_FLAGS bits 31:29.
    void setPatchConfigSource(uint8_t source)
    {
        bootFlags[3] = static_cast<uint8_t>((bootFlags[3] & 0x1F) | ((source & 0x07) << 5));
    }

    void writeBytes(uint32_t addr, const uint8_t *src, size_t len)
    {
        for (size_t i = 0; i < len && addr + i < eeprom.size(); ++i) eeprom[addr + i] = src[i];
    }

    void writeLe32(uint32_t addr, uint32_t value)
    {
        const uint8_t b[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                              static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
        writeBytes(addr, b, sizeof(b));
    }

    uint32_t readLe32(uint32_t addr) const
    {
        uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = (v << 8) | byteAt(addr + static_cast<uint32_t>(i));
        return v;
    }

    uint8_t byteAt(uint32_t addr) const { return addr < eeprom.size() ? eeprom[addr] : 0xFF; }

    // --- Fault injection -----------------------------------------------------

    // Applies to the executeCommand() call with this zero-based global index.
    void failCall(size_t callIndex, Fault f) { _rules.push_back(Rule{"", callIndex, std::move(f)}); }

    // Applies to the n-th (zero-based) call of task code cmd.
    void failNth(const char (&cmd)[5], size_t n, Fault f)
    {
        _rules.push_back(Rule{std::string(cmd, 4), n, std::move(f)});
    }

    // --- Log queries ---------------------------------------------------------

    size_t countOf(const char (&cmd)[5]) const
    {
        size_t n = 0;
        for (const Call &c : calls)
            if (c.cmd == std::string(cmd, 4)) ++n;
        return n;
    }

    // --- TPS25751CommandExecutor ---------------------------------------------

    TPS25751TaskResult executeCommand(TPS25751FourCC cmd, const uint8_t *inData, size_t inLen,
                                      uint8_t *outData, size_t outLen, uint32_t timeoutMs) override
    {
        const std::string code(cmd.code, 4);
        const size_t index = calls.size();
        now += msPerCommand;

        Call call{code, std::vector<uint8_t>(), outLen, timeoutMs, {TPS25751TaskStatus::Success, 0}};
        if (logPayloads && inData != nullptr && inLen > 0) call.in.assign(inData, inData + inLen);

        const Fault *fault = findFault(code, index);

        TPS25751TaskResult result{TPS25751TaskStatus::Success, 0};
        if (fault != nullptr && fault->status != TPS25751TaskStatus::Success) {
            // The task ran on the controller but the host saw a failure, for
            // example a COMMAND poll that timed out after the write completed.
            if (fault->runTask) {
                if (inData != nullptr && inLen > 0) memcpy(data, inData, inLen < kDataSize ? inLen : kDataSize);
                runTask(code, inData, inLen);
            }
            if (fault->hook) fault->hook(*this);
            result = TPS25751TaskResult{fault->status, 0};
            call.result = result;
            calls.push_back(std::move(call));
            return result;
        }

        if (inData != nullptr && inLen > 0) {
            memcpy(data, inData, inLen < kDataSize ? inLen : kDataSize);
        }

        TPS25751TaskStatus status = TPS25751TaskStatus::Success;
        if (fault == nullptr || fault->runTask || fault->returnCode < 0) {
            status = runTask(code, inData, inLen);
        }
        if (fault != nullptr && fault->returnCode >= 0) data[0] = static_cast<uint8_t>(fault->returnCode);
        if (fault != nullptr && fault->hook) fault->hook(*this);

        if (status == TPS25751TaskStatus::Success) {
            if (outData != nullptr && outLen > 0) memcpy(outData, data, outLen < kDataSize ? outLen : kDataSize);
            result = TPS25751TaskResult{TPS25751TaskStatus::Success, data[0]};
        } else {
            result = TPS25751TaskResult{status, 0};
        }
        call.result = result;
        calls.push_back(std::move(call));
        return result;
    }

    bool readMode(TPS25751Mode &out) override
    {
        ++modeReads;
        if (modeReadFails || stillRebooting()) return false;
        out = TPS25751Mode(mode);
        return true;
    }

    bool readBootFlags(TPS25751BootFlags &out) override
    {
        ++bootFlagsReads;
        if (bootFlagsReadFails || stillRebooting()) return false;
        out = TPS25751BootFlags(bootFlags);
        return true;
    }

    void delayMs(uint32_t ms) override
    {
        delays.push_back(ms);
        now += ms;
    }

    uint32_t nowMs() override { return now; }

private:
    struct Rule
    {
        std::string cmd;  // empty: match the global call index
        size_t n;
        Fault fault;
    };
    std::vector<Rule> _rules;

    size_t countOf4(const std::string &code) const
    {
        size_t n = 0;
        for (const Call &c : calls)
            if (c.cmd == code) ++n;
        return n;
    }

    // The first rule that matches, in the order the rules were added. The
    // per-code call number is counted only when a rule for this code exists:
    // counting walks the whole log, which a sweep over thousands of updates
    // cannot afford on every call.
    const Fault *findFault(const std::string &code, size_t index) const
    {
        bool counted = false;
        size_t nth = 0;
        for (const Rule &r : _rules) {
            if (r.cmd.empty()) {
                if (r.n == index) return &r.fault;
                continue;
            }
            if (r.cmd != code) continue;
            if (!counted) {
                nth = countOf4(code);
                counted = true;
            }
            if (r.n == nth) return &r.fault;
        }
        return nullptr;
    }

    bool stillRebooting()
    {
        if (!rebooting) return false;
        if (static_cast<uint32_t>(now - rebootStartedAt) < rebootDurationMs) return true;
        rebooting = false;
        memcpy(mode, modeAfterReset, kModeSize);
        return false;
    }

    static uint32_t le32(const uint8_t *in, size_t inLen)
    {
        uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = (v << 8) | (static_cast<size_t>(i) < inLen ? in[i] : 0);
        return v;
    }

    // Applies the task's effect to the model and writes its output to DATA.
    TPS25751TaskStatus runTask(const std::string &code, const uint8_t *in, size_t inLen)
    {
        if (code == "FLrd") {
            // Byte order verified in hardware stage 1; 0xFF past the end is an
            // assumption, not in the TRM. So is the write address staying where
            // it was: the TRM does not say whether FLrd moves it.
            const uint32_t addr = le32(in, inLen);
            for (uint32_t i = 0; i < 16; ++i) data[i] = byteAt(addr + i);
            if (flrdMovesWriteAddress) {  // hypothetical, off by default
                writeAddress = addr + 16;
                writeAddressSet = true;
            }
            return TPS25751TaskStatus::Success;
        }
        if (code == "FLad") {
            const uint32_t addr = le32(in, inLen);
            if (addr >= eeprom.size()) {  // assumption, not in the TRM
                data[0] = kRcRejected;
                return TPS25751TaskStatus::Success;
            }
            writeAddress = addr;
            writeAddressSet = true;
            data[0] = kRcSuccess;
            return TPS25751TaskStatus::Success;
        }
        if (code == "FLwd") {
            // Rejecting these with 0x03 is an assumption, not in the TRM: Table
            // 4-21 documents only 0x00 and 0xFF. Writing exactly inLen bytes is
            // not documented either; stage 2 verified it for a 4-byte write on
            // the test board, other lengths are a model. The address: the
            // write goes wherever the last FLad or FLwd left writeAddress, even
            // when a later FLad was faulted into having no effect (assumption,
            // not in the TRM).
            if (!writeAddressSet || inLen > kDataSize || writeAddress + inLen > eeprom.size()) {
                data[0] = kRcRejected;
                return TPS25751TaskStatus::Success;
            }
            writeBytes(writeAddress, in, inLen);
            writeAddress += static_cast<uint32_t>(inLen);
            data[0] = kRcSuccess;
            return TPS25751TaskStatus::Success;
        }
        if (code == "FLvy") {
            // Header_ID-only check: an assumption, not in the TRM.
            data[0] = (readLe32(le32(in, inLen)) == kHeaderId) ? kRcSuccess : kRcNotValid;
            return TPS25751TaskStatus::Success;
        }
        if (code == "GAID") {
            memset(data, 0, sizeof(data));
            writeAddressSet = false;  // assumption, not in the TRM
            ++resetCount;
            rebooting = true;
            rebootStartedAt = now;
            return TPS25751TaskStatus::Success;
        }
        return TPS25751TaskStatus::Rejected;
    }
};
