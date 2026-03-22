#include "crc.h"

/*
 * CRC16/CCITT
 * Polynomial: 0x1021
 * Initial value: 0xFFFF
 * Input/output reflection: none
 * Known vector: crc16("123456789", 9) == 0x29B1
 */
uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;

    for (i = 0; i < len; i++) {
        uint8_t byte = data[i];
        uint8_t bit;
        for (bit = 0; bit < 8; bit++) {
            uint16_t xor_flag = (crc ^ (byte << 8)) & 0x8000;
            crc <<= 1;
            if (xor_flag) {
                crc ^= 0x1021;
            }
            byte <<= 1;
        }
    }
    return crc;
}
