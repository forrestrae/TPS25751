#pragma once

// Native-test stand-in for the Arduino TwoWire class and the global Wire
// object, so that src/TPS25751.cpp and src/TPS25751PatchLoader.cpp compile in
// [env:native]. It has the calls those sources make and nothing more.
//
// TwoWire itself models no device. Each transaction is handed to a Target (see
// FakeTps25751Target.h), which answers it. With no target set, every write
// returns 2 (address NACK) and every read returns 0 bytes. The transmit buffer
// holds txCapacity bytes (136, as the Teensy 4 driver's buffer); write()
// returns how many bytes it accepted, as the real one does.

#include "Arduino.h"

class TwoWire
{
public:
    class Target
    {
    public:
        virtual ~Target() = default;
        // A write transaction: the address, the bytes after it, and whether it
        // ended with STOP. Returns the endTransmission() code: 0 ACK, 2 address
        // NACK, 3 data NACK, 4 other error.
        virtual uint8_t onWrite(uint8_t addr, const uint8_t *data, size_t len, bool stop) = 0;
        // A read of quantity bytes. Fills out and returns the bytes received,
        // which a failing transaction makes fewer than quantity.
        virtual size_t onRead(uint8_t addr, size_t quantity, uint8_t *out) = 0;
    };

    static constexpr size_t kBufferSize = 256;

    Target *target = nullptr;
    size_t txCapacity = 136;
    unsigned beginCalls = 0;
    uint32_t clockHz = 0;

    void begin() { ++beginCalls; }
    void setClock(uint32_t hz) { clockHz = hz; }

    void beginTransmission(uint8_t addr)
    {
        _txAddr = addr;
        _txLen = 0;
    }
    void beginTransmission(int addr) { beginTransmission(static_cast<uint8_t>(addr)); }

    size_t write(uint8_t b)
    {
        if (_txLen >= txCapacity || _txLen >= kBufferSize) return 0;
        _tx[_txLen++] = b;
        return 1;
    }
    size_t write(const uint8_t *data, size_t n)
    {
        size_t i = 0;
        while (i < n && write(data[i]) == 1) ++i;
        return i;
    }

    uint8_t endTransmission(bool sendStop = true)
    {
        const size_t len = _txLen;
        _txLen = 0;
        return target ? target->onWrite(_txAddr, _tx, len, sendStop) : 2;
    }

    size_t requestFrom(uint8_t addr, size_t quantity, bool = true)
    {
        if (quantity > kBufferSize) quantity = kBufferSize;
        _rxLen = target ? target->onRead(addr, quantity, _rx) : 0;
        _rxPos = 0;
        return _rxLen;
    }

    int available() const { return static_cast<int>(_rxLen - _rxPos); }
    int read() { return _rxPos < _rxLen ? _rx[_rxPos++] : -1; }

private:
    uint8_t _tx[kBufferSize] = {};
    size_t _txLen = 0;
    uint8_t _txAddr = 0;
    uint8_t _rx[kBufferSize] = {};
    size_t _rxLen = 0;
    size_t _rxPos = 0;
};

// One Wire object per test program, as on the target.
inline TwoWire Wire;
