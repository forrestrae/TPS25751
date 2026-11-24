#pragma once

// Scripted TPS25751 behind the native TwoWire fake (Wire.h), for the suites of
// src/TPS25751.cpp and src/TPS25751PatchLoader.cpp.
//
// What it models, and where the TI documents are the source:
//   - The register protocol of the unique address (0x20 by default): a write
//     of [reg] without STOP sets the register a following read returns; a
//     write of [reg, count, data...] with STOP writes the register; a read
//     returns [count, data...], count being the register size (TRM, I2C
//     interface). A read longer than count + 1 is padded with 0xFF.
//   - COMMAND (0x08) and DATA (0x09) as a 4CC task: writing a code to COMMAND
//     starts it; the next pollsBeforeDone reads of COMMAND still return the
//     code, and the read after that completes the task, returning 4 zero bytes
//     or "!CMD" (TRM Table 4-1 and the 4CC description). On completion DATA
//     takes the task's scripted output.
//   - PBMs and PBMc (TRM Tables 4-15 and 4-16, SLVAFV8A): PBMs answers DATA[0]
//     = PatchStartStatus (0 by default) and, when that is 0, opens the burst
//     port at 0x30; PBMc answers DATA[0] = 0 by default and sets MODE to
//     "APP " modeAppDelayMs after it completes. The burst port acknowledges
//     only while open and records every byte.
//   - Unknown 4CC codes, and any task with reject set, complete as "!CMD".
//
// Choices the documents do not make, which a test that relies on them pins
// the fake and not the device: the poll count before a task completes, the
// 0xFF padding, MODE switching at an exact time, and the fault model below.
//
// Faults: a rule matches a transaction by its index in the log, by a time
// window on the stub clock, by address, by register (the register pointer for
// reads), and by kind. A matching write returns writeError (2 NACK, 4, or 5,
// the codes the Teensy driver returns) without effect; a
// matching read returns only readBytes of the normal response. A task's
// onDone hook can add rules, for example a window that starts when the task
// completes.
//
// Every transaction goes to the log with the stub clock's time.

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"
#include "Wire.h"
#include "TPS25751RegisterAddress.h"

class FakeTps25751Target : public TwoWire::Target
{
public:
    static constexpr uint8_t kRegMode = 0x03;
    static constexpr uint8_t kRegCommand = 0x08;
    static constexpr uint8_t kRegData = 0x09;
    static constexpr uint8_t kRegIntMask1 = 0x16;
    static constexpr uint8_t kRegIntClear1 = 0x18;
    static constexpr uint8_t kPbmAddress = 0x30;

    enum class Kind { Write, Read };

    struct Txn
    {
        uint64_t timeUs;
        Kind kind;
        uint8_t addr;
        int reg;                     // register addressed (pointer for reads), -1 for raw bursts
        std::vector<uint8_t> bytes;  // written bytes, or the bytes the read returned
        bool stop;
        uint8_t result;              // write: endTransmission code; read: 0
        size_t requested;            // read: quantity asked for
    };

    struct Fault
    {
        long index = -1;          // match this log index only, -1 for any
        uint64_t fromUs = 0;      // match while fromUs <= now < toUs, when toUs > fromUs
        uint64_t toUs = 0;
        int addr = -1;            // -1 for any
        int reg = -1;             // -1 for any
        int kind = -1;            // 0 write, 1 read, -1 any
        uint8_t writeError = 2;   // endTransmission code for a matched write: 2, 4, or 5, as the Teensy driver returns
        size_t readBytes = 0;     // bytes a matched read returns
        int remaining = -1;       // matches left, -1 for unlimited
    };

    struct TaskScript
    {
        uint32_t pollsBeforeDone = 1;
        bool reject = false;
        std::vector<uint8_t> output;  // DATA bytes from byte 0 on completion
        std::function<void(FakeTps25751Target &)> onDone;
    };

    explicit FakeTps25751Target(uint8_t address = 0x20) : address(address)
    {
        setReg(kRegMode, {'P', 'T', 'C', 'H'});
        setReg(kRegCommand, {0, 0, 0, 0});
        setReg(kRegData, std::vector<uint8_t>(64, 0));
        tasks["PBMs"] = TaskScript{1, false, {0x00}, nullptr};
        tasks["PBMc"] = TaskScript{1, false, {0x00}, nullptr};
    }

    // --- State -------------------------------------------------------------------

    uint8_t address;
    std::map<uint8_t, std::vector<uint8_t>> regs;
    std::map<std::string, TaskScript> tasks;
    std::vector<Fault> faults;
    std::vector<Txn> log;

    bool burstOpen = false;
    std::vector<uint8_t> burst;
    uint32_t modeAppDelayMs = 0;
    std::string lastCompleted;   // 4CC of the last completed task
    uint64_t lastCompletedUs = 0;

    // --- Setup -------------------------------------------------------------------

    void setReg(uint8_t reg, std::vector<uint8_t> bytes) { regs[reg] = std::move(bytes); }
    void setMode(const char (&m)[5]) { setReg(kRegMode, {uint8_t(m[0]), uint8_t(m[1]), uint8_t(m[2]), uint8_t(m[3])}); }
    std::string mode() const { return std::string(regs.at(kRegMode).begin(), regs.at(kRegMode).end()); }

    // --- Queries -----------------------------------------------------------------

    static uint64_t now() { return fake_arduino::g_nowUs; }

    bool isRead(const Txn &t, uint8_t reg) const { return t.kind == Kind::Read && t.addr == address && t.reg == reg; }
    bool isRegWrite(const Txn &t, uint8_t reg) const
    {
        return t.kind == Kind::Write && t.addr == address && t.reg == reg && t.bytes.size() >= 2;
    }
    size_t count(std::function<bool(const Txn &)> pred) const
    {
        size_t n = 0;
        for (const Txn &t : log) n += pred(t) ? 1 : 0;
        return n;
    }

    // --- TwoWire::Target ---------------------------------------------------------

    uint8_t onWrite(uint8_t addr, const uint8_t *data, size_t len, bool stop) override
    {
        Txn t{now(), Kind::Write, addr, (addr == kPbmAddress || len == 0) ? -1 : data[0],
              std::vector<uint8_t>(data, data + len), stop, 0, 0};
        if (Fault *f = match(t)) {
            t.result = f->writeError;
            log.push_back(t);
            return t.result;
        }
        t.result = applyWrite(addr, data, len, stop);
        log.push_back(t);
        return t.result;
    }

    size_t onRead(uint8_t addr, size_t quantity, uint8_t *out) override
    {
        Txn t{now(), Kind::Read, addr, addr == address ? _pointer : -1, {}, true, 0, quantity};
        std::vector<uint8_t> response;
        if (addr == address) response = readResponse(_pointer, quantity);
        if (Fault *f = match(t)) {
            if (response.size() > f->readBytes) response.resize(f->readBytes);
        }
        memcpy(out, response.data(), response.size());
        t.bytes = response;
        log.push_back(t);
        return response.size();
    }

private:
    int _pointer = -1;
    std::string _pending;
    uint32_t _pollsLeft = 0;
    uint64_t _modeAppAtUs = 0;
    bool _modeSwitchPending = false;

    Fault *match(const Txn &t)
    {
        const long index = static_cast<long>(log.size());
        for (Fault &f : faults) {
            if (f.remaining == 0) continue;
            if (f.index >= 0 && f.index != index) continue;
            if (f.toUs > f.fromUs && (t.timeUs < f.fromUs || t.timeUs >= f.toUs)) continue;
            if (f.addr >= 0 && f.addr != t.addr) continue;
            if (f.reg >= 0 && f.reg != t.reg) continue;
            if (f.kind >= 0 && f.kind != (t.kind == Kind::Write ? 0 : 1)) continue;
            if (f.remaining > 0) --f.remaining;
            return &f;
        }
        return nullptr;
    }

    uint8_t applyWrite(uint8_t addr, const uint8_t *data, size_t len, bool stop)
    {
        if (addr == kPbmAddress) {
            if (!burstOpen) return 2;
            burst.insert(burst.end(), data, data + len);
            return 0;
        }
        if (addr != address) return 2;
        if (len == 0) return 0;
        _pointer = data[0];
        if (len == 1 && !stop) return 0;
        if (len < 2) return 0;
        std::vector<uint8_t> &r = regs[data[0]];
        const size_t n = len - 2;
        if (r.size() < n) r.resize(n, 0);
        memcpy(r.data(), data + 2, n);
        if (data[0] == kRegCommand && n == 4) startTask(std::string(reinterpret_cast<const char *>(data + 2), 4));
        return 0;
    }

    void startTask(const std::string &code)
    {
        _pending = code;
        auto it = tasks.find(code);
        _pollsLeft = (it != tasks.end()) ? it->second.pollsBeforeDone : 0;
    }

    void completeTask()
    {
        auto it = tasks.find(_pending);
        const bool known = (it != tasks.end());
        std::vector<uint8_t> &cmd = regs[kRegCommand];
        if (!known || it->second.reject) {
            cmd = {'!', 'C', 'M', 'D'};
        } else {
            cmd = {0, 0, 0, 0};
            std::vector<uint8_t> &dataReg = regs[kRegData];
            const std::vector<uint8_t> &output = it->second.output;
            for (size_t i = 0; i < output.size() && i < dataReg.size(); ++i) dataReg[i] = output[i];
            if (_pending == "PBMs") burstOpen = (!output.empty() && output[0] == 0);
            if (_pending == "PBMc" && !output.empty() && output[0] == 0) {
                burstOpen = false;
                _modeSwitchPending = true;
                _modeAppAtUs = now() + static_cast<uint64_t>(modeAppDelayMs) * 1000;
            }
        }
        lastCompleted = _pending;
        lastCompletedUs = now();
        _pending.clear();
        if (known && it->second.onDone) it->second.onDone(*this);
    }

    std::vector<uint8_t> readResponse(int reg, size_t quantity)
    {
        if (reg < 0) return std::vector<uint8_t>(quantity, 0xFF);
        if (reg == kRegCommand && !_pending.empty()) {
            if (_pollsLeft > 0) {
                --_pollsLeft;
            } else {
                completeTask();
            }
        }
        if (_modeSwitchPending && now() >= _modeAppAtUs) {
            setMode("APP ");
            _modeSwitchPending = false;
        }
        auto it = regs.find(static_cast<uint8_t>(reg));
        std::vector<uint8_t> data;
        if (it != regs.end()) {
            data = it->second;
        } else {
            const uint8_t size = TPS25751Registers::getRegisterSize(static_cast<TPS25751Registers::Address>(reg));
            data.assign(size, 0);
        }
        std::vector<uint8_t> response;
        response.push_back(static_cast<uint8_t>(data.size()));
        response.insert(response.end(), data.begin(), data.end());
        response.resize(quantity, 0xFF);
        return response;
    }
};
