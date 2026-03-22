#ifndef UMESH_CRC_H
#define UMESH_CRC_H

#include <stdint.h>

/*
 * CRC16/CCITT — polynomial 0x1021, init 0xFFFF
 * Known vector: "123456789" -> 0x29B1
 */
uint16_t crc16(const uint8_t *data, uint16_t len);

#endif /* UMESH_CRC_H */
