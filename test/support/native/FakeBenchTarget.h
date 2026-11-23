#pragma once

// The TPS25751 the bench console suite talks to, behind the native TwoWire fake
// (Wire.h).
//
// FakeTps25751Target.h scripts each task by a poll count and injects faults per
// transaction, for the suites that pin the order and timing of the library's own
// transactions. The console's cases are sequences over time instead: a restart
// holds COMMAND, takes the controller off the bus, and brings it back with
// another MODE. This target is a register table with hooks for that:
//
// - Register framing. A read returns the length byte and then the data; a write
//   carries the register address, a length byte, and the data (CONSTRAINTS.md,
//   I2C Protocol). The length byte is the register's size unless lengthByte
//   overrides it, and a read past the register is padded with zeros.
// - The 4CC handshake. A write to COMMAND (0x08) runs taskHandler with the code
//   and the DATA register at once; the handler's verdict decides whether COMMAND
//   clears, reads "!CMD", or keeps the code (TRM SLVUCR8B section 4.1, p.61).
// - INT_CLEAR1 (0x18): a write clears the matching bits of INT_EVENT1 (0x14).
// - The Patch Burst Mode port at I2C address 0x30, which collects raw bytes and
//   is always open.
// - Faults and time: the target can stop answering, fail the reads or writes of
//   one register, keep a register's value across a write, and run scheduled
//   changes when the stub clock reaches a time.
//
// The task completing at the COMMAND write, the zero padding, the open burst
// port, and the times of scheduled changes are choices of this target, not
// documented behaviour. A test that depends on one pins the target, not the
// device.
//
// Tests fill `registers` directly and read `writes`, `reads`, and `tasks` back.

#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Arduino.h"
#include "Wire.h"

class FakeBenchTarget : public TwoWire::Target
{
public:
    static constexpr uint8_t kCommand = 0x08;
    static constexpr uint8_t kData = 0x09;
    static constexpr uint8_t kIntEvent = 0x14;
    static constexpr uint8_t kIntClear = 0x18;
    static constexpr uint8_t kPbmAddress = 0x30;
    static constexpr size_t kDataSize = 64;

    /// What the controller does with a 4CC task.
    enum class Verdict
    {
        Complete,  ///< COMMAND clears to 0
        Reject,    ///< COMMAND reads "!CMD"
        Pending    ///< COMMAND keeps the code until something else clears it
    };

    struct Write
    {
        uint8_t reg;
        std::vector<uint8_t> data;
    };

    using TaskHandler = std::function<Verdict(const std::string &code, std::vector<uint8_t> &data)>;

    explicit FakeBenchTarget(uint8_t address = 0x20) : _address(address)
    {
        registers[kCommand] = std::vector<uint8_t>(4, 0);
        registers[kData] = std::vector<uint8_t>(kDataSize, 0);
    }

    // ----- state a test sets up -----

    /// Register contents by address. The length byte of a read is the vector's
    /// size unless lengthByte overrides it.
    std::map<uint8_t, std::vector<uint8_t>> registers;

    /// Length byte to report for a register instead of its data size.
    std::map<uint8_t, uint8_t> lengthByte;

    /// Registers whose reads return nothing (a NACK on that read only).
    std::map<uint8_t, bool> failRead;

    /// Registers whose writes are not acknowledged.
    std::map<uint8_t, bool> failWrite;

    /// Registers that accept a write and keep their old value.
    std::map<uint8_t, bool> ignoreWrite;

    /// Runs when a 4CC code is written to COMMAND. Without one, every task
    /// completes with DATA untouched.
    TaskHandler taskHandler;

    /// Runs after each accepted register write, with the register address.
    std::function<void(FakeBenchTarget &, uint8_t reg)> afterWrite;

    /// Runs before each read is answered, with the register address and the
    /// number of bytes the host asked for. It can change the state the read
    /// then sees, which places an event between two reads of one command.
    std::function<void(FakeBenchTarget &, uint8_t reg, size_t quantity)> beforeRead;

    /// True while the target does not answer at all.
    bool silent = false;

    // ----- what the library did, for assertions -----

    std::vector<Write> writes;                         ///< register writes, in order
    std::vector<std::pair<uint8_t, size_t>> reads;     ///< (register, bytes requested)
    std::vector<std::string> tasks;                    ///< 4CC codes written to COMMAND
    std::vector<uint8_t> pbmBytes;                     ///< bytes sent to I2C address 0x30
    size_t transactions = 0;                           ///< write and read transactions seen

    // ----- helpers -----

    void setRegister(uint8_t reg, std::initializer_list<uint8_t> bytes)
    {
        registers[reg] = std::vector<uint8_t>(bytes);
    }

    void setRegister(uint8_t reg, const uint8_t *bytes, size_t length)
    {
        registers[reg] = std::vector<uint8_t>(bytes, bytes + length);
    }

    void setMode(const char *fourChars)
    {
        registers[0x03] = std::vector<uint8_t>(fourChars, fourChars + 4);
    }

    /// Runs @p action at the first transaction at or after stub-clock time @p atMillis.
    void schedule(unsigned long atMillis, std::function<void(FakeBenchTarget &)> action)
    {
        _scheduled.push_back({atMillis, std::move(action)});
    }

    /// Number of writes recorded for one register.
    size_t writeCount(uint8_t reg) const
    {
        size_t n = 0;
        for (const Write &w : writes)
            if (w.reg == reg) ++n;
        return n;
    }

    /// The most recent write to a register, or nullptr.
    const Write *lastWrite(uint8_t reg) const
    {
        for (size_t i = writes.size(); i > 0; --i)
            if (writes[i - 1].reg == reg) return &writes[i - 1];
        return nullptr;
    }

    // ----- TwoWire::Target -----

    uint8_t onWrite(uint8_t address, const uint8_t *bytes, size_t length, bool stop) override
    {
        (void)stop;
        runScheduled();
        ++transactions;
        if (silent) return 2;

        if (address == kPbmAddress)
        {
            pbmBytes.insert(pbmBytes.end(), bytes, bytes + length);
            return 0;
        }
        if (address != _address || length == 0) return 2;

        _pointer = bytes[0];
        if (length == 1) return 0;  // register pointer only: a read follows

        // bytes[1] is the length byte the host sends; the data follows it.
        const std::vector<uint8_t> data(bytes + 2, bytes + length);
        if (failWrite.count(_pointer) != 0) return 2;
        writes.push_back({_pointer, data});
        applyWrite(_pointer, data);
        if (afterWrite) afterWrite(*this, _pointer);
        return 0;
    }

    size_t onRead(uint8_t address, size_t quantity, uint8_t *out) override
    {
        runScheduled();
        ++transactions;
        if (silent || address != _address) return 0;
        if (beforeRead) beforeRead(*this, _pointer, quantity);
        if (failRead.count(_pointer) != 0) return 0;

        reads.push_back({_pointer, quantity});
        const auto found = registers.find(_pointer);
        const std::vector<uint8_t> empty;
        const std::vector<uint8_t> &data = (found != registers.end()) ? found->second : empty;
        const auto forced = lengthByte.find(_pointer);
        out[0] = (forced != lengthByte.end()) ? forced->second : static_cast<uint8_t>(data.size());
        // A host that clocks more bytes than the register holds reads zeros.
        for (size_t i = 0; i + 1 < quantity; ++i) out[i + 1] = (i < data.size()) ? data[i] : 0;
        return quantity;
    }

private:
    struct Scheduled
    {
        unsigned long at;
        std::function<void(FakeBenchTarget &)> action;
    };

    void runScheduled()
    {
        for (size_t i = 0; i < _scheduled.size();)
        {
            if (millis() >= _scheduled[i].at)
            {
                auto action = std::move(_scheduled[i].action);
                _scheduled.erase(_scheduled.begin() + static_cast<long>(i));
                action(*this);
            }
            else
            {
                ++i;
            }
        }
    }

    void applyWrite(uint8_t reg, const std::vector<uint8_t> &data)
    {
        if (ignoreWrite.count(reg) != 0) return;

        if (reg == kCommand)
        {
            const std::string code(data.begin(), data.end());
            tasks.push_back(code);
            Verdict verdict = Verdict::Complete;
            if (taskHandler) verdict = taskHandler(code, registers[kData]);
            if (verdict == Verdict::Complete)
                registers[kCommand] = std::vector<uint8_t>(4, 0);
            else if (verdict == Verdict::Reject)
                registers[kCommand] = {'!', 'C', 'M', 'D'};
            else
                registers[kCommand] = data;
            return;
        }
        if (reg == kData)
        {
            // The host writes only the bytes a task needs; the rest keep their value.
            std::vector<uint8_t> &dataReg = registers[kData];
            dataReg.resize(kDataSize, 0);
            for (size_t i = 0; i < data.size() && i < kDataSize; ++i) dataReg[i] = data[i];
            return;
        }
        if (reg == kIntClear)
        {
            std::vector<uint8_t> &event = registers[kIntEvent];
            for (size_t i = 0; i < data.size() && i < event.size(); ++i)
                event[i] = static_cast<uint8_t>(event[i] & ~data[i]);
            return;
        }
        registers[reg] = data;
    }

    uint8_t _address;
    uint8_t _pointer = 0;
    std::vector<Scheduled> _scheduled;
};
