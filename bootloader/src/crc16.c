#include "crc16.h"

uint16_t crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    int bit;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}
