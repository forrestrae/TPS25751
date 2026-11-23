#include "TPS25751HostExecutor.h"
#include "TPS25751.h"
#include <Arduino.h>

TPS25751HostExecutor::TPS25751HostExecutor(TPS25751& pd)
    : _pd(pd)
{
}

TPS25751TaskResult TPS25751HostExecutor::executeCommand(TPS25751FourCC cmd,
    const uint8_t* inData, size_t inLen,
    uint8_t* outData, size_t outLen, uint32_t timeoutMs)
{
    return _pd.executeCommand(cmd, inData, inLen, outData, outLen, timeoutMs);
}

bool TPS25751HostExecutor::readMode(TPS25751Mode& mode)
{
    return _pd.readMode(mode);
}

bool TPS25751HostExecutor::readBootFlags(TPS25751BootFlags& flags)
{
    return _pd.readBootFlags(flags);
}

void TPS25751HostExecutor::delayMs(uint32_t ms)
{
    delay(ms);
}

uint32_t TPS25751HostExecutor::nowMs()
{
    return millis();
}
