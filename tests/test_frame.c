#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../src/mac/frame.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond, name) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", name); \
            s_pass++; \
        } else { \
            printf("  FAIL: %s  (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

static void fill_frame(umesh_frame_t *frame,
                       uint8_t src, uint8_t dst,
                       uint8_t link_src, uint8_t link_dst,
                       uint16_t seq, uint8_t hop,
                       uint8_t cmd, uint8_t flags,
                       const uint8_t *payload, uint16_t payload_len)
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->src = src;
    frame->dst = dst;
    frame->link_src = link_src;
    frame->link_dst = link_dst;
    frame->seq_num = seq;
    frame->hop_count = hop;
    frame->cmd = cmd;
    frame->flags = flags;
    frame->payload_len = payload_len;
    if (payload && payload_len > 0) {
        memcpy(frame->payload, payload, payload_len);
    }
}

static void assert_frame_eq(const umesh_frame_t *a, const umesh_frame_t *b, const char *prefix)
{
    char label[128];

    snprintf(label, sizeof(label), "%s: wire_version", prefix);
    TEST_ASSERT(a->wire_version == b->wire_version, label);
    snprintf(label, sizeof(label), "%s: net_id", prefix);
    TEST_ASSERT(a->net_id == b->net_id, label);
    snprintf(label, sizeof(label), "%s: src", prefix);
    TEST_ASSERT(a->src == b->src, label);
    snprintf(label, sizeof(label), "%s: dst", prefix);
    TEST_ASSERT(a->dst == b->dst, label);
    snprintf(label, sizeof(label), "%s: link_src", prefix);
    TEST_ASSERT(a->link_src == b->link_src, label);
    snprintf(label, sizeof(label), "%s: link_dst", prefix);
    TEST_ASSERT(a->link_dst == b->link_dst, label);
    snprintf(label, sizeof(label), "%s: seq_num", prefix);
    TEST_ASSERT(a->seq_num == b->seq_num, label);
    snprintf(label, sizeof(label), "%s: hop_count", prefix);
    TEST_ASSERT(a->hop_count == b->hop_count, label);
    snprintf(label, sizeof(label), "%s: cmd", prefix);
    TEST_ASSERT(a->cmd == b->cmd, label);
    snprintf(label, sizeof(label), "%s: flags", prefix);
    TEST_ASSERT(a->flags == b->flags, label);
    snprintf(label, sizeof(label), "%s: payload_len", prefix);
    TEST_ASSERT(a->payload_len == b->payload_len, label);
    if (a->payload_len > 0) {
        snprintf(label, sizeof(label), "%s: payload", prefix);
        TEST_ASSERT(memcmp(a->payload, b->payload, a->payload_len) == 0, label);
    }
}

static void test_roundtrip_valid(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    size_t len = 0;
    umesh_result_t r;
    static const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02 };

    fill_frame(&orig, 0x03, 0x05, 0x03, 0x04, 0x0123, 3,
               UMESH_CMD_SENSOR_TEMP, UMESH_FLAG_ACK_REQ | UMESH_FLAG_PRIO_HIGH,
               data, (uint16_t)sizeof(data));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "roundtrip: serialize OK");
    TEST_ASSERT(len == UMESH_FRAME_MIN_SIZE + sizeof(data), "roundtrip: correct length");

    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_OK, "roundtrip: deserialize OK");
    assert_frame_eq(&orig, &out, "roundtrip");
}

static void test_minimum_legal_frame(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[64];
    size_t len = 0;
    umesh_result_t r;

    fill_frame(&orig, 0x02, 0x01, 0x02, UMESH_ADDR_COORDINATOR, 0x0001, 1,
               UMESH_CMD_PING, UMESH_FLAG_PRIO_BULK, NULL, 0);

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "minimum: serialize OK");
    TEST_ASSERT(len == UMESH_FRAME_MIN_SIZE, "minimum: exact minimum size");

    memset(&out, 0xA5, sizeof(out));
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_OK, "minimum: deserialize OK");
    assert_frame_eq(&orig, &out, "minimum");
}

static void test_maximum_legal_frame(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[256];
    size_t len = 0;
    uint8_t payload[UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE];
    size_t i;
    umesh_result_t r;

    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i ^ 0xA5u);
    }

    fill_frame(&orig, 0x04, 0x06, 0x04, 0x07, 0x1ABC, 7,
               UMESH_CMD_SENSOR_RAW, UMESH_FLAG_ENCRYPTED | UMESH_FLAG_PRIO_NORMAL,
               payload, (uint16_t)sizeof(payload));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "maximum: serialize OK");
    TEST_ASSERT(len == UMESH_FRAME_MIN_SIZE + sizeof(payload), "maximum: exact size");

    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_OK, "maximum: deserialize OK");
    assert_frame_eq(&orig, &out, "maximum");
}

static void test_truncation_boundaries(void)
{
    umesh_frame_t orig;
    uint8_t buf[128];
    size_t len = 0;
    size_t i;
    umesh_result_t r;
    static const uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };

    fill_frame(&orig, 0x08, 0x09, 0x08, 0x09, 0x0200, 2,
               UMESH_CMD_STATUS, UMESH_FLAG_PRIO_LOW,
               data, (uint16_t)sizeof(data));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "truncation: serialize OK");

    for (i = 0; i < len; i++) {
        umesh_frame_t scratch;
        r = frame_deserialize(buf, i, &scratch);
        TEST_ASSERT(r != UMESH_OK, "truncation: truncated frame rejected");
    }
}

static void test_oversized_payload(void)
{
    umesh_frame_t orig;
    uint8_t buf[256];
    size_t len = 0;
    umesh_result_t r;

    fill_frame(&orig, 0x02, 0x03, 0x02, 0x03, 0x0010, 4,
               UMESH_CMD_PING, UMESH_FLAG_PRIO_NORMAL,
               NULL, (uint16_t)(UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE + 1));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_ERR_TOO_LONG, "oversized: rejected");
}

static void test_trailing_bytes(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    size_t len = 0;
    umesh_result_t r;
    static const uint8_t data[] = { 0xAA, 0xBB };

    fill_frame(&orig, 0x03, 0x04, 0x03, 0x04, 0x0011, 1,
               UMESH_CMD_PONG, UMESH_FLAG_PRIO_NORMAL,
               data, (uint16_t)sizeof(data));
    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "trailing: serialize OK");
    buf[len] = 0xFF;
    r = frame_deserialize(buf, len + 1, &out);
    TEST_ASSERT(r == UMESH_ERR_TOO_LONG, "trailing: rejected");
}

static void test_invalid_version(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    size_t len = 0;
    umesh_result_t r;

    fill_frame(&orig, 0x02, 0x03, 0x02, 0x03, 0x0012, 1,
               UMESH_CMD_PING, UMESH_FLAG_PRIO_NORMAL, NULL, 0);
    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "version: serialize OK");
    buf[0] = 0x7F;
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_ERR_INVALID_DST, "version: rejected");
}

static void test_invalid_flags(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    size_t len = 0;
    umesh_result_t r;

    fill_frame(&orig, 0x02, 0x03, 0x02, 0x03, 0x0013, 1,
               UMESH_CMD_PING, UMESH_FLAG_PRIO_NORMAL, NULL, 0);
    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "flags: serialize OK");
    buf[10] |= 0x08;
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_ERR_INVALID_DST, "flags: rejected");
}

static void test_crc_corruption(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    size_t len = 0;
    static const uint8_t data[] = { 0x11, 0x22, 0x33 };
    umesh_result_t r;

    fill_frame(&orig, 0x02, 0x03, 0x02, 0x03, 0x0020, 2,
               UMESH_CMD_PING, UMESH_FLAG_PRIO_NORMAL,
               data, (uint16_t)sizeof(data));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "crc: serialize OK");
    buf[UMESH_FRAME_WIRE_HEADER_SIZE + 1] ^= 0xFF;
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_ERR_CRC_FAIL, "crc: corruption detected");
}

static void test_malformed_addresses(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    size_t len = 0;
    umesh_result_t r;

    fill_frame(&orig, 0x02, 0x03, 0x02, 0x03, 0x0021, 2,
               UMESH_CMD_PING, UMESH_FLAG_PRIO_NORMAL, NULL, 0);
    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "addr: serialize OK");

    buf[2] = UMESH_ADDR_UNASSIGNED;
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_ERR_INVALID_DST, "addr: orig_src rejected");

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "addr: reserialize OK");
    buf[5] = UMESH_ADDR_UNASSIGNED;
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_ERR_INVALID_DST, "addr: link_dst rejected");
}

static void test_random_input(void)
{
    uint8_t buf[64];
    umesh_frame_t out;
    uint32_t state = 0x12345678u;
    size_t i;
    umesh_result_t r;

    for (i = 0; i < sizeof(buf); i++) {
        state = state * 1103515245u + 12345u;
        buf[i] = (uint8_t)(state >> 16);
    }
    buf[0] = 0x00;
    r = frame_deserialize(buf, sizeof(buf), &out);
    TEST_ASSERT(r != UMESH_OK, "random: garbage rejected");
}

static void test_final_vs_link_dest(void)
{
    umesh_frame_t orig, parsed, forwarded;
    uint8_t buf[128];
    size_t len = 0;
    static const uint8_t data[] = { 0x55, 0x66 };
    umesh_result_t r;

    fill_frame(&orig, 0x03, 0x09, 0x03, 0x04, 0x0202, 5,
               UMESH_CMD_ROUTE_UPDATE, UMESH_FLAG_PRIO_NORMAL,
               data, (uint16_t)sizeof(data));
    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "forwarding: initial serialize OK");

    r = frame_deserialize(buf, len, &parsed);
    TEST_ASSERT(r == UMESH_OK, "forwarding: initial deserialize OK");
    TEST_ASSERT(parsed.dst == orig.dst, "forwarding: final destination preserved");
    TEST_ASSERT(parsed.link_dst == orig.link_dst, "forwarding: initial link destination preserved");

    forwarded = parsed;
    forwarded.link_src = 0x04;
    forwarded.link_dst = 0x05;
    r = frame_serialize(&forwarded, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "forwarding: reserialize next hop OK");

    r = frame_deserialize(buf, len, &parsed);
    TEST_ASSERT(r == UMESH_OK, "forwarding: reparse next hop OK");
    TEST_ASSERT(parsed.dst == orig.dst, "forwarding: final destination unchanged");
    TEST_ASSERT(parsed.link_dst == 0x05, "forwarding: link destination changed");
    TEST_ASSERT(parsed.link_src == 0x04, "forwarding: link source updated");
}

int main(void)
{
    printf("=== test_frame ===\n");
    test_roundtrip_valid();
    test_minimum_legal_frame();
    test_maximum_legal_frame();
    test_truncation_boundaries();
    test_oversized_payload();
    test_trailing_bytes();
    test_invalid_version();
    test_invalid_flags();
    test_crc_corruption();
    test_malformed_addresses();
    test_random_input();
    test_final_vs_link_dest();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
