#include "TPS25751DownstreamDevice.h"
#include "TPS25751Debug.h"

#include <cstring>

namespace {
    /// Bit 7 of the I2C target address is reserved (must be 0) in the I2Cr/I2Cw DATA payload
    constexpr uint8_t kAddressMask = 0x7F;

    constexpr size_t kI2CrInputLen = 3;
    // DATA is 64 bytes and DATA[0] holds the task return code, so at most 63 bytes
    // of downstream read data fit in a single I2Cr (DATA[1..63]). (The TRM table
    // numbers output bytes 2-65, implying a 65-byte region; if hardware testing
    // confirms that, bump DATA's modeled size and this limit together.)
    constexpr size_t kI2CrMaxReadLen = 63;

    // I2Cw input header is {targetAddr, Length, regOffset} per TRM Table 5-25
    // ('I2Cw'), followed by the payload bytes.
    constexpr size_t kI2CwHeaderLen = 3;
    // Payload occupies DATA bytes 4-14 (11 bytes); bytes beyond byte 14 are ignored.
    constexpr size_t kI2CwMaxPayloadLen = 11;
}

TPS25751DownstreamDevice::TPS25751DownstreamDevice(const TPS25751& host, uint8_t deviceAddress)
    : _host(host), _deviceAddress(static_cast<uint8_t>(deviceAddress & kAddressMask)),
      _lastI2CrMs(0), _lastI2CwMs(0)
{
}

void TPS25751DownstreamDevice::enforceSpacing(uint32_t& lastMs, const char* taskName) const
{
    const uint32_t now = millis();
    // lastMs == 0 is the "never issued yet" sentinel; do not warn on the first call.
    if (lastMs != 0 && (now - lastMs) < kMinCommandSpacingMs) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK,
                        "TPS25751DownstreamDevice: %s issued only %lu ms after the previous %s "
                        "(TRM requires >=%lu ms spacing between consecutive %s commands)",
                        taskName, (unsigned long)(now - lastMs), taskName,
                        (unsigned long)kMinCommandSpacingMs, taskName);
    }
    lastMs = now;
}

bool TPS25751DownstreamDevice::readRegister(uint8_t devReg, uint8_t* buf, size_t len) const
{
    if (buf == nullptr || len == 0 || len > kI2CrMaxReadLen) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "TPS25751DownstreamDevice::readRegister: invalid len=%u", (unsigned)len);
        return false;
    }

    enforceSpacing(_lastI2CrMs, "I2Cr");

    // I2Cr input (3 B): {targetAddr(7-bit), regOffset, numBytes}
    const uint8_t in[kI2CrInputLen] = {
        _deviceAddress,
        devReg,
        static_cast<uint8_t>(len)
    };

    uint8_t raw[kI2CrMaxReadLen + 1] = {0}; // DATA[0]=return code, DATA[1..]=read bytes
    TPS25751TaskResult result = _host.executeCommand(
        TPS25751FourCC::of("I2Cr"), in, sizeof(in), raw, sizeof(raw));

    if (result.status != TPS25751TaskStatus::Success) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "TPS25751DownstreamDevice::readRegister: I2Cr task did not succeed (status=%d)",
                        (int)result.status);
        return false;
    }
    if (result.returnCode != 0) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "TPS25751DownstreamDevice::readRegister: I2Cr returnCode=0x%02X", result.returnCode);
        return false;
    }

    // Read data starts at DATA offset 1 (DATA[0] is the task return code).
    memcpy(buf, raw + 1, len);
    return true;
}

bool TPS25751DownstreamDevice::writeRegister(uint8_t devReg, const uint8_t* data, size_t len) const
{
    if (data == nullptr || len == 0 || len > kI2CwMaxPayloadLen) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "TPS25751DownstreamDevice::writeRegister: invalid len=%u", (unsigned)len);
        return false;
    }

    enforceSpacing(_lastI2CwMs, "I2Cw");

    // I2Cw input per TRM Table 5-25: {targetAddr(7-bit), Length, regOffset, payload...}.
    // Hardware finding: the controller transmits exactly `Length` bytes starting at
    // the register-offset byte, so Length must count the offset byte PLUS the payload
    // (Length = len + 1). With Length == len the controller sent only the offset byte
    // (a pointer-only write: ACKed, no NACK, register unchanged).
    // NOTE: this is NOT the I2Cr byte order {addr, regOffset, numBytes}; I2Cw swaps the
    // length and offset positions, and its count includes the offset byte.
    uint8_t in[kI2CwHeaderLen + kI2CwMaxPayloadLen];
    in[0] = _deviceAddress;
    in[1] = static_cast<uint8_t>(len + 1);
    in[2] = devReg;
    memcpy(in + kI2CwHeaderLen, data, len);

    const size_t inLen = kI2CwHeaderLen + len;

    uint8_t raw[1] = {0}; // DATA[0]=task return code (write queued, not necessarily completed)
    TPS25751TaskResult result = _host.executeCommand(
        TPS25751FourCC::of("I2Cw"), in, inLen, raw, sizeof(raw));

    if (result.status != TPS25751TaskStatus::Success) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "TPS25751DownstreamDevice::writeRegister: I2Cw task did not succeed (status=%d)",
                        (int)result.status);
        return false;
    }
    if (result.returnCode != 0) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "TPS25751DownstreamDevice::writeRegister: I2Cw returnCode=0x%02X", result.returnCode);
        return false;
    }

    return true;
}
