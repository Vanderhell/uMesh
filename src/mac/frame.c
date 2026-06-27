#include "frame.h"
#include <string.h>

static bool is_node_addr(uint8_t addr)
{
    return addr >= 0x01 && addr <= 0xFD;
}

static bool is_link_addr(uint8_t addr)
{
    return addr == UMESH_ADDR_BROADCAST || is_node_addr(addr);
}

static bool is_join_src_addr(uint8_t addr, uint8_t cmd)
{
    if (addr == UMESH_ADDR_UNASSIGNED && cmd == UMESH_CMD_JOIN) {
        return true;
    }
    return is_node_addr(addr);
}

static bool is_join_link_src_addr(uint8_t addr, uint8_t cmd)
{
    if (addr == UMESH_ADDR_UNASSIGNED && cmd == UMESH_CMD_JOIN) {
        return true;
    }
    return is_node_addr(addr);
}

static bool frame_fields_valid(const umesh_frame_t *frame)
{
    if (!frame) return false;
    if (frame->wire_version != UMESH_WIRE_VERSION) return false;
    if (frame->net_id == UMESH_ADDR_BROADCAST ||
        frame->net_id == UMESH_ADDR_UNASSIGNED) return false;
    if (!is_join_src_addr(frame->src, frame->cmd)) return false;
    if (!is_link_addr(frame->dst)) return false;
    if (!is_join_link_src_addr(frame->link_src, frame->cmd)) return false;
    if (!is_link_addr(frame->link_dst)) return false;
    if ((frame->flags & ~UMESH_FLAG_VALID_MASK) != 0u) return false;
    return true;
}

static bool checked_add_size(size_t a, size_t b, size_t *out)
{
    if (SIZE_MAX - a < b) return false;
    *out = a + b;
    return true;
}

umesh_result_t frame_serialize(const umesh_frame_t *frame,
                               uint8_t *buf, size_t buf_size,
                               size_t *out_len)
{
    size_t total;
    size_t payload_off;
    size_t crc_off;
    uint16_t crc;

    if (!frame || !buf || !out_len) {
        return UMESH_ERR_NULL_PTR;
    }

    if (frame->payload_len > (UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE)) {
        return UMESH_ERR_TOO_LONG;
    }

    if (!frame_fields_valid(frame)) {
        return UMESH_ERR_INVALID_DST;
    }

    if (!checked_add_size(UMESH_FRAME_WIRE_HEADER_SIZE,
                          frame->payload_len,
                          &payload_off)) {
        return UMESH_ERR_TOO_LONG;
    }
    if (!checked_add_size(payload_off, UMESH_FRAME_WIRE_TRAILER_SIZE, &total)) {
        return UMESH_ERR_TOO_LONG;
    }
    if (total > buf_size) {
        return UMESH_ERR_TOO_LONG;
    }

    buf[0]  = frame->wire_version;
    buf[1]  = frame->net_id;
    buf[2]  = frame->src;
    buf[3]  = frame->dst;
    buf[4]  = frame->link_src;
    buf[5]  = frame->link_dst;
    buf[6]  = (uint8_t)(frame->seq_num & 0xFF);
    buf[7]  = (uint8_t)(frame->seq_num >> 8);
    buf[8]  = frame->hop_count;
    buf[9]  = frame->cmd;
    buf[10] = frame->flags;
    buf[11] = (uint8_t)(frame->payload_len & 0xFF);
    buf[12] = (uint8_t)(frame->payload_len >> 8);

    if (frame->payload_len > 0) {
        memcpy(&buf[UMESH_FRAME_WIRE_HEADER_SIZE], frame->payload, frame->payload_len);
    }

    crc_off = UMESH_FRAME_WIRE_HEADER_SIZE + frame->payload_len;
    crc = crc16(buf, (uint16_t)crc_off);
    buf[crc_off]     = (uint8_t)(crc & 0xFF);
    buf[crc_off + 1] = (uint8_t)(crc >> 8);

    *out_len = total;
    return UMESH_OK;
}

static void clear_frame(umesh_frame_t *frame)
{
    if (frame) {
        memset(frame, 0, sizeof(*frame));
    }
}

umesh_result_t frame_deserialize(const uint8_t *buf, size_t len,
                                 umesh_frame_t *frame)
{
    size_t payload_len;
    size_t expected_len;
    size_t crc_off;
    uint16_t received_crc;
    uint16_t computed_crc;
    uint16_t encoded_len;
    uint8_t cmd;

    if (!buf || !frame) {
        return UMESH_ERR_NULL_PTR;
    }

    clear_frame(frame);

    if (len < UMESH_FRAME_MIN_SIZE) {
        return UMESH_ERR_TOO_LONG;
    }

    if (buf[0] != UMESH_WIRE_VERSION) {
        return UMESH_ERR_INVALID_DST;
    }

    cmd = buf[9];
    if ((buf[10] & ~UMESH_FLAG_VALID_MASK) != 0u) {
        return UMESH_ERR_INVALID_DST;
    }
    if (buf[1] == UMESH_ADDR_BROADCAST ||
        buf[1] == UMESH_ADDR_UNASSIGNED ||
        (!is_join_src_addr(buf[2], cmd)) ||
        !is_link_addr(buf[3]) ||
        (!is_join_link_src_addr(buf[4], cmd)) ||
        !is_link_addr(buf[5])) {
        return UMESH_ERR_INVALID_DST;
    }

    encoded_len = (uint16_t)(buf[11] | ((uint16_t)buf[12] << 8));
    payload_len = encoded_len;

    if (payload_len > (UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE)) {
        return UMESH_ERR_TOO_LONG;
    }

    if (!checked_add_size(UMESH_FRAME_WIRE_HEADER_SIZE, payload_len, &expected_len)) {
        return UMESH_ERR_TOO_LONG;
    }
    if (!checked_add_size(expected_len, UMESH_FRAME_WIRE_TRAILER_SIZE, &expected_len)) {
        return UMESH_ERR_TOO_LONG;
    }

    if (len != expected_len) {
        return UMESH_ERR_TOO_LONG;
    }

    crc_off = UMESH_FRAME_WIRE_HEADER_SIZE + payload_len;
    received_crc = (uint16_t)(buf[crc_off] |
                   ((uint16_t)buf[crc_off + 1] << 8));
    computed_crc = crc16(buf, (uint16_t)crc_off);

    if (received_crc != computed_crc) {
        return UMESH_ERR_CRC_FAIL;
    }

    frame->wire_version = buf[0];
    frame->net_id       = buf[1];
    frame->src          = buf[2];
    frame->dst          = buf[3];
    frame->link_src     = buf[4];
    frame->link_dst     = buf[5];
    frame->seq_num      = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));
    frame->hop_count    = buf[8];
    frame->cmd          = buf[9];
    frame->flags        = buf[10];
    frame->payload_len  = payload_len;
    frame->crc          = received_crc;

    if (payload_len > 0) {
        memcpy(frame->payload, &buf[UMESH_FRAME_WIRE_HEADER_SIZE], payload_len);
    }

    return UMESH_OK;
}
