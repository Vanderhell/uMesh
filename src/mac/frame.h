#ifndef UMESH_FRAME_H
#define UMESH_FRAME_H

#include "../common/defs.h"
#include "../common/crc.h"

/* Wire format header size (without payload and CRC):
 * net_id(1) + dst(1) + src(1) + flags(1) + cmd(1) +
 * payload_len(1) + seq_num(2) + hop_count(1) = 9 bytes
 */
#define UMESH_FRAME_HEADER_SIZE  9

umesh_result_t frame_serialize(const umesh_frame_t *frame,
                               uint8_t *buf, uint16_t buf_size,
                               uint8_t *out_len);

umesh_result_t frame_deserialize(const uint8_t *buf, uint8_t len,
                                 umesh_frame_t *frame);

#endif /* UMESH_FRAME_H */
