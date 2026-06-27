#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/context.h"
#include "../src/sec/sec.h"

static int s_pass = 0;
static int s_fail = 0;
static umesh_ctx_t s_ctx;

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

static const uint8_t TEST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static void bind_ctx(uint32_t epoch)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    umesh_bind_ctx(&s_ctx);
    s_ctx.cfg.security_epoch = epoch;
}

static umesh_result_t init_security(uint32_t epoch)
{
    bind_ctx(epoch);
    return sec_init(TEST_KEY, 0x01, UMESH_SEC_FULL);
}

static void fill_frame(umesh_frame_t *frame,
                       uint16_t seq_num,
                       uint8_t cmd,
                       const uint8_t *payload,
                       uint16_t payload_len)
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->src = 0x03;
    frame->dst = 0x02;
    frame->link_src = 0x03;
    frame->link_dst = 0x02;
    frame->seq_num = seq_num;
    frame->hop_count = 4;
    frame->cmd = cmd;
    frame->flags = UMESH_FLAG_ENCRYPTED;
    frame->payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(frame->payload, payload, payload_len);
    }
}

static void test_counter_increments_from_one_canonical_source(void)
{
    static const uint8_t payload[] = { 0xDE, 0xAD };
    umesh_frame_t a, b;
    umesh_result_t r;

    r = init_security(0x1000);
    TEST_ASSERT(r == UMESH_OK, "counter: init OK");

    fill_frame(&a, 0, UMESH_CMD_SENSOR_TEMP, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&a);
    TEST_ASSERT(r == UMESH_OK, "counter: first encrypt OK");
    TEST_ASSERT(s_ctx.sec.protected_counter == 1, "counter: first increment");
    TEST_ASSERT(a.seq_num == 1, "counter: first seq assigned");

    fill_frame(&b, 0, UMESH_CMD_SENSOR_TEMP, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&b);
    TEST_ASSERT(r == UMESH_OK, "counter: second encrypt OK");
    TEST_ASSERT(s_ctx.sec.protected_counter == 2, "counter: second increment");
    TEST_ASSERT(b.seq_num == 2, "counter: second seq assigned");
}

static void test_init_fails_without_required_epoch(void)
{
    bind_ctx(0);
    TEST_ASSERT(sec_init(TEST_KEY, 0x01, UMESH_SEC_FULL) == UMESH_ERR_INVALID_DST,
                "init: missing epoch rejected");
}

static void test_nonce_differs_after_newer_epoch(void)
{
    static const uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
    umesh_frame_t a, b;
    umesh_result_t r;

    r = init_security(0x2000);
    TEST_ASSERT(r == UMESH_OK, "nonce: first init OK");

    fill_frame(&a, 0, UMESH_CMD_PING, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&a);
    TEST_ASSERT(r == UMESH_OK, "nonce: first encrypt OK");

    r = init_security(0x2001);
    TEST_ASSERT(r == UMESH_OK, "nonce: second init OK");

    fill_frame(&b, 0, UMESH_CMD_PING, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&b);
    TEST_ASSERT(r == UMESH_OK, "nonce: second encrypt OK");
    TEST_ASSERT(memcmp(a.payload, b.payload, a.payload_len) != 0,
                "nonce: ciphertext differs after newer epoch");
}

static void test_duplicate_protected_packet_rejected(void)
{
    static const uint8_t payload[] = { 0xAA, 0xBB, 0xCC };
    umesh_frame_t frame, duplicate;
    umesh_result_t r;

    r = init_security(0x3000);
    TEST_ASSERT(r == UMESH_OK, "duplicate: init OK");

    fill_frame(&frame, 0, UMESH_CMD_PING, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "duplicate: encrypt OK");

    duplicate = frame;
    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "duplicate: first decrypt OK");

    r = sec_decrypt_frame(&duplicate);
    TEST_ASSERT(r == UMESH_ERR_REPLAY, "duplicate: second decrypt rejected");
}

static void test_stale_epoch_rejected(void)
{
    static const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    umesh_frame_t frame;
    umesh_result_t r;

    r = init_security(0x4000);
    TEST_ASSERT(r == UMESH_OK, "stale: old init OK");

    fill_frame(&frame, 0, UMESH_CMD_PING, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "stale: encrypt OK");

    r = init_security(0x4001);
    TEST_ASSERT(r == UMESH_OK, "stale: new init OK");

    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_ERR_MIC_FAIL, "stale: old epoch rejected");
}

static void test_tampered_protected_header_rejected(void)
{
    static const uint8_t payload[] = { 0x55, 0x66 };
    umesh_frame_t frame;
    umesh_result_t r;

    r = init_security(0x5000);
    TEST_ASSERT(r == UMESH_OK, "header: init OK");

    fill_frame(&frame, 0, UMESH_CMD_PING, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "header: encrypt OK");

    frame.link_dst ^= 0x01;
    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_ERR_MIC_FAIL, "header: tamper rejected");
}

static void test_tampered_tag_rejected(void)
{
    static const uint8_t payload[] = { 0x77, 0x88, 0x99 };
    umesh_frame_t frame;
    umesh_result_t r;

    r = init_security(0x6000);
    TEST_ASSERT(r == UMESH_OK, "tag: init OK");

    fill_frame(&frame, 0, UMESH_CMD_PING, payload, (uint16_t)sizeof(payload));
    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "tag: encrypt OK");

    frame.payload[frame.payload_len - 1] ^= 0xFF;
    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_ERR_MIC_FAIL, "tag: tamper rejected");
}

static void test_reordered_packet_within_window_accepted(void)
{
    static const uint8_t payload_a[] = { 0x10, 0x11 };
    static const uint8_t payload_b[] = { 0x20, 0x21 };
    umesh_frame_t a, b;
    umesh_result_t r;

    r = init_security(0x7000);
    TEST_ASSERT(r == UMESH_OK, "reorder: init OK");

    fill_frame(&a, 5, UMESH_CMD_PING, payload_a, (uint16_t)sizeof(payload_a));
    r = sec_encrypt_frame(&a);
    TEST_ASSERT(r == UMESH_OK, "reorder: first encrypt OK");

    fill_frame(&b, 4, UMESH_CMD_PING, payload_b, (uint16_t)sizeof(payload_b));
    r = sec_encrypt_frame(&b);
    TEST_ASSERT(r == UMESH_OK, "reorder: second encrypt OK");

    r = sec_decrypt_frame(&a);
    TEST_ASSERT(r == UMESH_OK, "reorder: newer packet accepted");

    r = sec_decrypt_frame(&b);
    TEST_ASSERT(r == UMESH_OK, "reorder: older packet within window accepted");
}

int main(void)
{
    printf("=== test_sec ===\n");
    test_counter_increments_from_one_canonical_source();
    test_init_fails_without_required_epoch();
    test_nonce_differs_after_newer_epoch();
    test_duplicate_protected_packet_rejected();
    test_stale_epoch_rejected();
    test_tampered_protected_header_rejected();
    test_tampered_tag_rejected();
    test_reordered_packet_within_window_accepted();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
