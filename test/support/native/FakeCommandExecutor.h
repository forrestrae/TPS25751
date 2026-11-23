#pragma once

// Scripted TPS25751CommandExecutor for the native suites.
//
// Models what the flash 4CC tasks touch, closely enough to test a sequencer
// without hardware:
//
//   - a byte-array EEPROM (erased to 0xFF) of a configurable size;
//   - the 64-byte DATA register: the input payload is written to DATA bytes
//     0..inLen-1, the task writes its output, and outData is copied from DATA
//     byte 0, as TPS25751::executeCommand() does. returnCode is always DATA[0];
//   - FLrd (TRM Table 4-19): 16 bytes from the 4-byte little-endian address,
//     0xFF past the end of the EEPROM, no return-code byte;
//   - FLad (Table 4-20): loads the write-address register; 0x00, or 0x03
//     (rejected) for an address past the end;
//   - FLwd (Table 4-21): writes inLen bytes (up to 64) at the write address and
//     auto-increments it; 0x00, or 0x03 when no FLad was issued since the last
//     reset or the write would run past the end;
//   - FLvy (Table 4-22): 0x00 when the 4 bytes at the address are the
//     little-endian Header_ID 0xACE00001, else 0x01;
//   - GAID (Table 4-3): clears DATA and the write address, counts the reset, and
//     makes MODE and BOOT_STATUS reads fail for rebootDurationMs of fake time,
//     after which MODE reads modeAfterReset;
//   - any other code: COMMAND reads "!CMD" (Rejected);
//   - MODE and BOOT_STATUS register contents, with read failures on demand;
//   - a fake clock: delayMs() advances it, and every executeCommand() advances
//     it by msPerCommand;
//   - a log of every executeCommand() call (4CC, input payload, outLen,
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
    static constexpr size_t kBootSize = TPS25751Registers::Registers::BOOT_STATUS.size;  // 5

    static constexpr uint8_t kRcSuccess = 0x00;   // TRM Table 4-1
    static constexpr uint8_t kRcRejected = 0x03;  // TRM Table 4-1
    static constexpr uint8_t kRcBusy = 0xFF;      // FLwd "flash is busy", Table 4-21
    static constexpr uint8_t kRcNotValid = 0x01;  // FLvy "not valid", Table 4-22
    static constexpr uint32_t kHeaderId = 0xACE00001UL;

    // BOOT_STATUS bit positions (TRM, register 0x2D "Boot Flags").
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
        memset(bootStatus, 0, sizeof(bootStatus));
    }

    // --- State (public so tests can inspect and edit it directly) -----------

    std::vector<uint8_t> eeprom;
    uint8_t data[kDataSize];
    uint8_t mode[kModeSize];
    uint8_t modeAfterReset[kModeSize];
    uint8_t bootStatus[kBootSize];

    uint32_t writeAddress = 0;
    bool writeAddressSet = false;

    uint32_t now = 0;
    uint32_t msPerCommand = 0;
    std::vector<uint32_t> delays;

    bool modeReadFails = false;
    bool bootFlagsReadFails = false;
    size_t modeReads = 0;
    size_t bootFlagsReads = 0;

    uint32_t rebootDurationMs = 1000;
    bool rebooting = false;
    uint32_t rebootEndsAt = 0;
    size_t resetCount = 0;

    std::vector<Call> calls;

    // --- Setup helpers -------------------------------------------------------

    void setMode(const char (&m)[5]) { memcpy(mode, m, kModeSize); }
    void setModeAfterReset(const char (&m)[5]) { memcpy(modeAfterReset, m, kModeSize); }

    void setBootBit(uint8_t bit, bool value)
    {
        const uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
        if (value) bootStatus[bit / 8] |= mask;
        else bootStatus[bit / 8] &= static_cast<uint8_t>(~mask);
    }

    // Patch Config Source, BOOT_STATUS bits 31:29.
    void setPatchConfigSource(uint8_t source)
    {
        bootStatus[3] = static_cast<uint8_t>((bootStatus[3] & 0x1F) | ((source & 0x07) << 5));
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
        const size_t nth = countOf4(code);
        now += msPerCommand;

        Call call{code, std::vector<uint8_t>(), outLen, timeoutMs, {TPS25751TaskStatus::Success, 0}};
        if (inData != nullptr && inLen > 0) call.in.assign(inData, inData + inLen);

        const Fault *fault = findFault(code, index, nth);

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
            calls.push_back(call);
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
        calls.push_back(call);
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
        out = TPS25751BootFlags(bootStatus);
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

    const Fault *findFault(const std::string &code, size_t index, size_t nth) const
    {
        for (const Rule &r : _rules) {
            if (r.cmd.empty() ? (r.n == index) : (r.cmd == code && r.n == nth)) return &r.fault;
        }
        return nullptr;
    }

    bool stillRebooting()
    {
        if (!rebooting) return false;
        if (now < rebootEndsAt) return true;
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
            const uint32_t addr = le32(in, inLen);
            for (uint32_t i = 0; i < 16; ++i) data[i] = byteAt(addr + i);
            return TPS25751TaskStatus::Success;
        }
        if (code == "FLad") {
            const uint32_t addr = le32(in, inLen);
            if (addr >= eeprom.size()) {
                data[0] = kRcRejected;
                return TPS25751TaskStatus::Success;
            }
            writeAddress = addr;
            writeAddressSet = true;
            data[0] = kRcSuccess;
            return TPS25751TaskStatus::Success;
        }
        if (code == "FLwd") {
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
            data[0] = (readLe32(le32(in, inLen)) == kHeaderId) ? kRcSuccess : kRcNotValid;
            return TPS25751TaskStatus::Success;
        }
        if (code == "GAID") {
            memset(data, 0, sizeof(data));
            writeAddressSet = false;
            ++resetCount;
            rebooting = true;
            rebootEndsAt = now + rebootDurationMs;
            return TPS25751TaskStatus::Success;
        }
        return TPS25751TaskStatus::Rejected;
    }
};
