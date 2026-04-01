#include <Arduino.h>


uint8_t extractBits(const uint8_t *data, const uint8_t bitPos, const uint8_t bitLen)
{
    uint16_t value = (data[bitPos / 8] >> (bitPos % 8));
    if (bitPos % 8 + bitLen > 8)
    {
        value |= (data[bitPos / 8 + 1] << (8 - (bitPos % 8)));
    }
    return value & ((1 << bitLen) - 1);
}