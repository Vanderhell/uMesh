#ifndef UMESH_FRAME_H
#define UMESH_FRAME_H

#include "../common/defs.h"
#include "../common/crc.h"

/* Versioned wire format v1:
 * [0]   version          u8
 * [1]   net_id           u8
 * [2]   orig_src         u8
 * [3]   final_dst        u8
 * [4]   link_src         u8
 * [5]   link_dst         u8
 * [6-7] session_counter  u16 little-endian
 * [8]   hop_limit        u8
 * [9]   cmd              u8
 * [10]  flags            u8
 * [11-12] payload_len    u16 little-endian
 * Payload follows, then CRC16 little-endian trailer.
 */
#define UMESH_FRAME_WIRE_HEADER_SIZE  13
#define UMESH_FRAME_WIRE_TRAILER_SIZE  2
#define UMESH_FRAME_MIN_SIZE          (UMESH_FRAME_WIRE_HEADER_SIZE + UMESH_FRAME_WIRE_TRAILER_SIZE)
#define UMESH_FRAME_HEADER_SIZE        UMESH_FRAME_WIRE_HEADER_SIZE

umesh_result_t frame_serialize(const umesh_frame_t *frame,
                               uint8_t *buf, size_t buf_size,
                               size_t *out_len);

umesh_result_t frame_deserialize(const uint8_t *buf, size_t len,
                                 umesh_frame_t *frame);

#endif /* UMESH_FRAME_H */
