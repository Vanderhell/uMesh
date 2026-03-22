#include <stdio.h>
#include <stdint.h>
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

static void test_frame_roundtrip_no_payload(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[64];
    uint8_t len = 0;
    umesh_result_t r;

    memset(&orig, 0, sizeof(orig));
    orig.net_id      = 0x01;
    orig.dst         = 0x02;
    orig.src         = 0x03;
    orig.flags       = UMESH_FLAG_IS_ACK;
    orig.cmd         = UMESH_CMD_PING;
    orig.payload_len = 0;
    orig.seq_num     = 0x0042;
    orig.hop_count   = 3;

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "roundtrip/no-payload: serialize OK");
    TEST_ASSERT(len == UMESH_FRAME_HEADER_SIZE + 2, "roundtrip/no-payload: correct length");

    memset(&out, 0, sizeof(out));
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_OK, "roundtrip/no-payload: deserialize OK");
    TEST_ASSERT(out.net_id      == orig.net_id,      "roundtrip/no-payload: net_id");
    TEST_ASSERT(out.dst         == orig.dst,          "roundtrip/no-payload: dst");
    TEST_ASSERT(out.src         == orig.src,          "roundtrip/no-payload: src");
    TEST_ASSERT(out.flags       == orig.flags,        "roundtrip/no-payload: flags");
    TEST_ASSERT(out.cmd         == orig.cmd,          "roundtrip/no-payload: cmd");
    TEST_ASSERT(out.payload_len == orig.payload_len,  "roundtrip/no-payload: payload_len");
    TEST_ASSERT(out.seq_num     == orig.seq_num,      "roundtrip/no-payload: seq_num");
    TEST_ASSERT(out.hop_count   == orig.hop_count,    "roundtrip/no-payload: hop_count");
}

static void test_frame_roundtrip_with_payload(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[128];
    uint8_t len = 0;
    umesh_result_t r;
    static const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02 };

    memset(&orig, 0, sizeof(orig));
    orig.net_id      = 0x01;
    orig.dst         = 0x05;
    orig.src         = 0x01;
    orig.flags       = UMESH_FLAG_ACK_REQ | UMESH_FLAG_PRIO_HIGH;
    orig.cmd         = UMESH_CMD_SENSOR_TEMP;
    orig.payload_len = sizeof(data);
    orig.seq_num     = 0x0123;
    orig.hop_count   = 1;
    memcpy(orig.payload, data, sizeof(data));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "roundtrip/payload: serialize OK");
    TEST_ASSERT(len == UMESH_FRAME_HEADER_SIZE + sizeof(data) + 2,
                "roundtrip/payload: correct length");

    memset(&out, 0, sizeof(out));
    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_OK, "roundtrip/payload: deserialize OK");
    TEST_ASSERT(out.payload_len == sizeof(data), "roundtrip/payload: payload_len");
    TEST_ASSERT(memcmp(out.payload, data, sizeof(data)) == 0,
                "roundtrip/payload: payload data matches");
}

static void test_frame_crc_error_detection(void)
{
    umesh_frame_t orig, out;
    uint8_t buf[64];
    uint8_t len = 0;
    umesh_result_t r;
    static const uint8_t data[] = { 0x11, 0x22, 0x33 };

    memset(&orig, 0, sizeof(orig));
    orig.net_id      = 0x01;
    orig.dst         = 0x02;
    orig.src         = 0x01;
    orig.cmd         = UMESH_CMD_PING;
    orig.payload_len = sizeof(data);
    orig.seq_num     = 0x0010;
    orig.hop_count   = 2;
    memcpy(orig.payload, data, sizeof(data));

    r = frame_serialize(&orig, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_OK, "crc-error: serialize OK");

    /* Corrupt one byte in the payload */
    buf[9] ^= 0xFF;

    r = frame_deserialize(buf, len, &out);
    TEST_ASSERT(r == UMESH_ERR_MIC_FAIL,
                "crc-error: CRC mismatch detected (UMESH_ERR_MIC_FAIL)");
}

static void test_frame_too_short(void)
{
    uint8_t buf[2] = { 0x01, 0x02 };
    umesh_frame_t out;
    umesh_result_t r;

    r = frame_deserialize(buf, 2, &out);
    TEST_ASSERT(r == UMESH_ERR_TOO_LONG, "too-short: correct error");
}

static void test_frame_null_ptr(void)
{
    uint8_t buf[64];
    uint8_t len = 0;
    umesh_result_t r;

    r = frame_serialize(NULL, buf, sizeof(buf), &len);
    TEST_ASSERT(r == UMESH_ERR_NULL_PTR, "null-ptr: serialize NULL frame");

    r = frame_deserialize(NULL, 10, NULL);
    TEST_ASSERT(r == UMESH_ERR_NULL_PTR, "null-ptr: deserialize NULL buf");
}

int main(void)
{
    printf("=== test_frame ===\n");
    test_frame_roundtrip_no_payload();
    test_frame_roundtrip_with_payload();
    test_frame_crc_error_detection();
    test_frame_too_short();
    test_frame_null_ptr();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
