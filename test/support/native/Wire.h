#pragma once

// Native-test stand-in for the Arduino TwoWire class and the global Wire
// object, so that src/TPS25751.cpp and src/TPS25751PatchLoader.cpp compile in
// [env:native]. It mirrors the calls, signatures, and observable limits of the
// Teensy 4 driver (WireIMXRT.cpp and WireIMXRT.h in framework-arduinoteensy
// 1.162.0) that the library relies on:
//   - write() returns 0 until beginTransmission() has been called once, as the
//     driver's `transmitting` flag does; endTransmission() does not clear the
//     flag or the buffer, as in the driver, and returns 4 when no
//     transmission was ever begun.
//   - The transmit buffer takes 136 data bytes after the address byte
//     (BUFFER_LENGTH + 1 with the address); write() returns the bytes it took.
//   - requestFrom() takes a uint8_t length (at least 1) and returns a uint8_t
//     count; the receive buffer holds 136 bytes, so a longer read returns 136.
//   - endTransmission() codes are the driver's: 0 success, 2 NACK (the driver
//     cannot tell an address NACK from a data NACK), 4 for a bus that never
//     became idle, lost arbitration, a pin held low, or its 50 ms timeout, and
//     5 for a FIFO error. It never returns 1 or 3.
//
// Not modelled: the driver's bus-idle wait and its forced clocking, clock
// stretching, the 50 ms timeouts, and requestFrom() returning 4 when the bus
// never became idle, which a caller cannot tell from a 4-byte read.
//
// TwoWire itself models no device. Each transaction is handed to a Target (see
// FakeTps25751Target.h), which answers it. With no target set, every write
// returns 2 and every read returns 0 bytes.

#include "Arduino.h"

class TwoWire
{
public:
    class Target
    {
    public:
        virtual ~Target() = default;
        // A write transaction: the address, the bytes after it, and whether it
        // ended with STOP. Returns the endTransmission() code: 0, 2, 4, or 5.
        virtual uint8_t onWrite(uint8_t addr, const uint8_t *data, size_t len, bool stop) = 0;
        // A read of quantity bytes. Fills out and returns the bytes received,
        // which a failing transaction makes fewer than quantity.
        virtual size_t onRead(uint8_t addr, size_t quantity, uint8_t *out) = 0;
    };

    static constexpr size_t kBufferLength = 136;  // BUFFER_LENGTH in WireIMXRT.h

    Target *target = nullptr;
    size_t txCapacity = kBufferLength;  // data bytes after the address; tests may lower it
    unsigned beginCalls = 0;
    uint32_t clockHz = 0;
    // Not driver behaviour: a fault for tests. requestFrom() still returns the
    // count the target gave, but the last rxMissing of those bytes are absent
    // from the receive buffer, so available() falls short of that count.
    size_t rxMissing = 0;

    void begin() { ++beginCalls; }
    void setClock(uint32_t hz) { clockHz = hz; }

    void beginTransmission(uint8_t addr)
    {
        _txAddr = addr;
        _txLen = 0;
        _transmitting = true;
    }
    void beginTransmission(int addr) { beginTransmission(static_cast<uint8_t>(addr)); }

    size_t write(uint8_t b)
    {
        if (!_transmitting || _txLen >= txCapacity || _txLen >= kBufferLength) return 0;
        _tx[_txLen++] = b;
        return 1;
    }
    size_t write(const uint8_t *data, size_t n)
    {
        size_t i = 0;
        while (i < n && write(data[i]) == 1) ++i;
        return i;
    }

    uint8_t endTransmission(uint8_t sendStop)
    {
        if (!_transmitting) return 4;  // the driver: "no address for transmit"
        return target ? target->onWrite(_txAddr, _tx, _txLen, sendStop != 0) : 2;
    }
    uint8_t endTransmission() { return endTransmission(static_cast<uint8_t>(1)); }

    uint8_t requestFrom(uint8_t addr, uint8_t quantity, uint8_t sendStop)
    {
        (void)sendStop;
        size_t n = quantity < 1 ? 1 : quantity;
        if (n > kBufferLength) n = kBufferLength;
        _rxLen = target ? target->onRead(addr, n, _rx) : 0;
        _rxPos = 0;
        const size_t reported = _rxLen;
        _rxLen = rxMissing < _rxLen ? _rxLen - rxMissing : 0;
        return static_cast<uint8_t>(reported);
    }
    uint8_t requestFrom(uint8_t addr, uint8_t quantity, bool sendStop)
    {
        return requestFrom(addr, quantity, static_cast<uint8_t>(sendStop ? 1 : 0));
    }
    uint8_t requestFrom(uint8_t addr, uint8_t quantity) { return requestFrom(addr, quantity, static_cast<uint8_t>(1)); }
    uint8_t requestFrom(int addr, int quantity, int sendStop)
    {
        return requestFrom(static_cast<uint8_t>(addr), static_cast<uint8_t>(quantity),
                           static_cast<uint8_t>(sendStop ? 1 : 0));
    }
    uint8_t requestFrom(int addr, int quantity)
    {
        return requestFrom(static_cast<uint8_t>(addr), static_cast<uint8_t>(quantity), static_cast<uint8_t>(1));
    }

    int available() const { return static_cast<int>(_rxLen - _rxPos); }
    int read() { return _rxPos < _rxLen ? _rx[_rxPos++] : -1; }

private:
    uint8_t _tx[kBufferLength] = {};
    size_t _txLen = 0;
    uint8_t _txAddr = 0;
    bool _transmitting = false;
    uint8_t _rx[kBufferLength] = {};
    size_t _rxLen = 0;
    size_t _rxPos = 0;
};

// One Wire object per test program, as on the target.
inline TwoWire Wire;
