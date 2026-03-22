#include "frame.h"
#include <string.h>

/*
 * Wire format:
 *  [0]    net_id       (1B)
 *  [1]    dst          (1B)
 *  [2]    src          (1B)
 *  [3]    flags        (1B)
 *  [4]    cmd          (1B)
 *  [5]    payload_len  (1B)
 *  [6-7]  seq_num      (2B, little-endian)
 *  [8]    hop_count    (1B)
 *  [9 .. 9+payload_len-1]  payload
 *  [9+payload_len .. 9+payload_len+1]  CRC16 (2B, little-endian)
 *
 *  Total header: 9 bytes
 *  Total frame:  9 + payload_len + 2 (CRC)
 */

umesh_result_t frame_serialize(const umesh_frame_t *frame,
                               uint8_t *buf, uint16_t buf_size,
                               uint8_t *out_len)
{
    uint8_t total;
    uint16_t crc;

    if (!frame || !buf || !out_len) {
        return UMESH_ERR_NULL_PTR;
    }

    if (frame->payload_len > UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE) {
        return UMESH_ERR_TOO_LONG;
    }

    total = (uint8_t)(UMESH_FRAME_HEADER_SIZE + frame->payload_len + 2u);
    if (total > buf_size) {
        return UMESH_ERR_TOO_LONG;
    }

    buf[0] = frame->net_id;
    buf[1] = frame->dst;
    buf[2] = frame->src;
    buf[3] = frame->flags;
    buf[4] = frame->cmd;
    buf[5] = frame->payload_len;
    buf[6] = (uint8_t)(frame->seq_num & 0xFF);
    buf[7] = (uint8_t)(frame->seq_num >> 8);
    buf[8] = frame->hop_count;

    if (frame->payload_len > 0) {
        memcpy(&buf[9], frame->payload, frame->payload_len);
    }

    /* CRC over header + payload (not CRC field itself) */
    crc = crc16(buf, (uint16_t)(UMESH_FRAME_HEADER_SIZE + frame->payload_len));
    buf[UMESH_FRAME_HEADER_SIZE + frame->payload_len]     = (uint8_t)(crc & 0xFF);
    buf[UMESH_FRAME_HEADER_SIZE + frame->payload_len + 1] = (uint8_t)(crc >> 8);

    *out_len = total;
    return UMESH_OK;
}

umesh_result_t frame_deserialize(const uint8_t *buf, uint8_t len,
                                 umesh_frame_t *frame)
{
    uint8_t payload_len;
    uint8_t expected_len;
    uint16_t received_crc;
    uint16_t computed_crc;

    if (!buf || !frame) {
        return UMESH_ERR_NULL_PTR;
    }

    if (len < UMESH_FRAME_HEADER_SIZE + 2u) {
        return UMESH_ERR_TOO_LONG;
    }

    payload_len  = buf[5];
    expected_len = (uint8_t)(UMESH_FRAME_HEADER_SIZE + payload_len + 2u);

    if (len < expected_len) {
        return UMESH_ERR_TOO_LONG;
    }

    if (payload_len > UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE) {
        return UMESH_ERR_TOO_LONG;
    }

    /* Verify CRC */
    received_crc = (uint16_t)(buf[UMESH_FRAME_HEADER_SIZE + payload_len] |
                   ((uint16_t)buf[UMESH_FRAME_HEADER_SIZE + payload_len + 1] << 8));
    computed_crc = crc16(buf, (uint16_t)(UMESH_FRAME_HEADER_SIZE + payload_len));

    if (received_crc != computed_crc) {
        return UMESH_ERR_MIC_FAIL;
    }

    frame->net_id      = buf[0];
    frame->dst         = buf[1];
    frame->src         = buf[2];
    frame->flags       = buf[3];
    frame->cmd         = buf[4];
    frame->payload_len = payload_len;
    frame->seq_num     = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));
    frame->hop_count   = buf[8];
    frame->crc         = received_crc;

    if (payload_len > 0) {
        memcpy(frame->payload, &buf[9], payload_len);
    }

    return UMESH_OK;
}
