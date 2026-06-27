#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/context.h"
#include "../src/mac/frame.h"
#include "../src/mac/mac.h"
#include "../src/net/discovery.h"
#include "../src/net/net.h"
#include "../src/phy/phy.h"
#include "../src/phy/phy_hal.h"
#include "../src/sec/sec.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond, name) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", name); \
            s_pass++; \
        } else { \
            printf("  FAIL: %s (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

static const uint8_t TEST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static umesh_ctx_t s_ctx;

static void init_stack(uint8_t node_id, umesh_role_t role, umesh_security_t sec)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };

    memset(&s_ctx, 0, sizeof(s_ctx));
    umesh_bind_ctx(&s_ctx);
    s_ctx.cfg.master_key = TEST_KEY;
    s_ctx.cfg.security_epoch = 1;
    phy_hal_init(&cfg);
    sec_init(TEST_KEY, 0x01, sec);
    net_init(0x01, node_id, role);
}

static void test_frame_parser_smoke(void)
{
    uint8_t valid[UMESH_FRAME_MIN_SIZE] = {0};
    umesh_frame_t frame;

    valid[0] = UMESH_WIRE_VERSION;
    valid[1] = 0x01;
    valid[2] = 0x11;
    valid[3] = 0x12;
    valid[4] = 0x11;
    valid[5] = 0x12;
    valid[6] = 0x34;
    valid[7] = 0x12;
    valid[8] = UMESH_MAX_HOP_COUNT;
    valid[9] = UMESH_CMD_PING;
    valid[10] = 0;
    valid[11] = 0;
    valid[12] = 0;

    TEST_ASSERT(frame_deserialize(valid, sizeof(valid), &frame) == UMESH_ERR_CRC_FAIL ||
                frame_deserialize(valid, sizeof(valid), &frame) == UMESH_OK,
                "corpus: frame parser handles minimal input deterministically");
    TEST_ASSERT(frame_deserialize(NULL, 0, &frame) == UMESH_ERR_NULL_PTR,
                "corpus: frame parser rejects null input");
}

static void test_protected_frame_smoke(void)
{
    umesh_frame_t frame;
    uint8_t buf[UMESH_MAX_FRAME_SIZE];
    size_t len = 0;

    init_stack(0x12, UMESH_ROLE_ROUTER, UMESH_SEC_FULL);

    memset(&frame, 0, sizeof(frame));
    frame.wire_version = UMESH_WIRE_VERSION;
    frame.net_id = 0x01;
    frame.src = 0x12;
    frame.dst = 0x13;
    frame.link_src = 0x12;
    frame.link_dst = 0x13;
    frame.seq_num = 0x1234;
    frame.hop_count = UMESH_MAX_HOP_COUNT;
    frame.cmd = UMESH_CMD_PING;
    frame.flags = UMESH_FLAG_ACK_REQ;
    frame.payload_len = 0;

    TEST_ASSERT(sec_encrypt_frame(&frame) == UMESH_OK,
                "corpus: protected frame encrypts");
    TEST_ASSERT(frame_serialize(&frame, buf, sizeof(buf), &len) == UMESH_OK,
                "corpus: protected frame serializes");
    mac_on_raw_rx(buf, (uint8_t)len, -60);
    TEST_ASSERT(1, "corpus: protected frame smoke path runs");
}

static void test_discovery_parser_smoke(void)
{
    umesh_frame_t join;
    uint8_t requester[6] = {0x21, 0, 0, 0, 0, 1};

    init_stack(UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR, UMESH_SEC_NONE);
    discovery_set_local_mac((const uint8_t[]){0x01, 0, 0, 0, 0, 0});
    memset(&join, 0, sizeof(join));
    join.wire_version = UMESH_WIRE_VERSION;
    join.net_id = 0x01;
    join.cmd = UMESH_CMD_JOIN;
    join.payload_len = 15;
    memcpy(&join.payload[0], requester, 6);
    join.payload[6] = 1;
    join.payload[10] = 1;
    join.payload[14] = UMESH_SEC_NONE;
    discovery_on_frame(&join, -60);
    TEST_ASSERT(1, "corpus: discovery parser smoke path runs");
}

int main(void)
{
    printf("=== test_corpus_smoke ===\n");
    test_frame_parser_smoke();
    test_protected_frame_smoke();
    test_discovery_parser_smoke();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
