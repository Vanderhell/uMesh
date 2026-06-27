#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/context.h"
#include "../src/mac/frame.h"
#include "../src/mac/mac.h"
#include "../src/net/net.h"
#include "../src/net/routing.h"
#include "../src/net/discovery.h"
#include "../src/phy/phy.h"
#include "../src/phy/phy_hal.h"
#include "../port/posix/phy_posix.h"
#include "../src/sec/sec.h"

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

static const uint8_t TEST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static umesh_ctx_t s_ctx_a;
static umesh_ctx_t s_ctx_b;
static umesh_ctx_t s_ctx_c;

static int s_rx_a = 0;
static int s_rx_b = 0;
static int s_rx_c = 0;

static uint8_t s_forward_buf[UMESH_MAX_FRAME_SIZE];
static size_t s_forward_len = 0;
static int s_forward_count = 0;

static void record_rx_a(const umesh_frame_t *frame, int8_t rssi)
{
    (void)frame;
    (void)rssi;
    s_rx_a++;
}

static void record_rx_b(const umesh_frame_t *frame, int8_t rssi)
{
    (void)frame;
    (void)rssi;
    s_rx_b++;
}

static void record_rx_c(const umesh_frame_t *frame, int8_t rssi)
{
    (void)frame;
    (void)rssi;
    s_rx_c++;
}

static void capture_forward(const uint8_t *payload, uint8_t len, int8_t rssi)
{
    (void)rssi;
    memcpy(s_forward_buf, payload, len);
    s_forward_len = len;
    s_forward_count++;
}

static void reset_observed(void)
{
    s_rx_a = 0;
    s_rx_b = 0;
    s_rx_c = 0;
    s_forward_len = 0;
    s_forward_count = 0;
    memset(s_forward_buf, 0, sizeof(s_forward_buf));
}

static void init_node(umesh_ctx_t *ctx, uint8_t node_id,
                      void (*rx_cb)(const umesh_frame_t *, int8_t))
{
    umesh_phy_cfg_t phy_cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };

    memset(ctx, 0, sizeof(*ctx));
    umesh_bind_ctx(ctx);
    phy_hal_init(&phy_cfg);
    mac_init(node_id);
    sec_init(TEST_KEY, 0x01, UMESH_SEC_NONE);
    net_init(0x01, node_id, UMESH_ROLE_ROUTER);
    net_set_rx_callback(rx_cb);
}

static void make_frame(umesh_frame_t *frame,
                       uint8_t src,
                       uint8_t dst,
                       uint8_t link_src,
                       uint8_t link_dst,
                       uint16_t seq_num,
                       uint8_t hop_limit,
                       uint8_t cmd)
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->src = src;
    frame->dst = dst;
    frame->link_src = link_src;
    frame->link_dst = link_dst;
    frame->seq_num = seq_num;
    frame->hop_count = hop_limit;
    frame->cmd = cmd;
    frame->flags = UMESH_FLAG_PRIO_NORMAL;
    frame->payload_len = 0;
}

static void inject_raw(umesh_ctx_t *ctx, const umesh_frame_t *frame)
{
    uint8_t buf[UMESH_MAX_FRAME_SIZE];
    size_t len = 0;

    umesh_bind_ctx(ctx);
    TEST_ASSERT(frame_serialize(frame, buf, sizeof(buf), &len) == UMESH_OK,
                "forwarding: serialize inject frame");
    mac_on_raw_rx(buf, (uint8_t)len, -60);
}

static void test_direct_delivery(void)
{
    umesh_frame_t frame;

    reset_observed();
    init_node(&s_ctx_b, 0x12, record_rx_b);
    phy_posix_set_loopback(true);

    make_frame(&frame, 0x11, 0x12, 0x11, 0x12, 0x1001, 5, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &frame);

    TEST_ASSERT(s_rx_b == 1, "A->B direct delivery");
    TEST_ASSERT(s_forward_count == 0, "A->B direct delivery does not forward");
}

static void test_three_node_forwarding(void)
{
    umesh_frame_t inbound;
    umesh_frame_t forwarded;

    reset_observed();
    init_node(&s_ctx_a, 0x11, record_rx_a);
    init_node(&s_ctx_b, 0x12, record_rx_b);
    init_node(&s_ctx_c, 0x13, record_rx_c);
    umesh_bind_ctx(&s_ctx_b);
    routing_add(0x13, 0x13, 1, -55, 0);
    phy_posix_set_loopback(true);
    phy_set_rx_cb(capture_forward);

    make_frame(&inbound, 0x11, 0x13, 0x11, 0x12, 0x1002, 5, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &inbound);

    TEST_ASSERT(s_rx_b == 0, "A->B->C does not deliver transit packet");
    TEST_ASSERT(s_forward_count == 1, "A->B->C forwarded once");
    TEST_ASSERT(frame_deserialize(s_forward_buf, s_forward_len, &forwarded) == UMESH_OK,
                "A->B->C forwarded frame parses");
    TEST_ASSERT(forwarded.src == 0x11, "A->B->C preserves original source");
    TEST_ASSERT(forwarded.dst == 0x13, "A->B->C preserves final destination");
    TEST_ASSERT(forwarded.link_src == 0x12, "A->B->C rewrites link source");
    TEST_ASSERT(forwarded.link_dst == 0x13, "A->B->C rewrites link destination");
    TEST_ASSERT(forwarded.hop_count == 4, "A->B->C decrements hop limit once");

    inject_raw(&s_ctx_c, &forwarded);
    TEST_ASSERT(s_rx_c == 1, "A->B->C delivers at final destination");
}

static void test_reverse_delivery(void)
{
    umesh_frame_t inbound;
    umesh_frame_t forwarded;

    reset_observed();
    init_node(&s_ctx_a, 0x11, record_rx_a);
    init_node(&s_ctx_b, 0x12, record_rx_b);
    init_node(&s_ctx_c, 0x13, record_rx_c);
    umesh_bind_ctx(&s_ctx_b);
    routing_add(0x11, 0x11, 1, -55, 0);
    phy_posix_set_loopback(true);
    phy_set_rx_cb(capture_forward);

    make_frame(&inbound, 0x13, 0x11, 0x13, 0x12, 0x1003, 5, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &inbound);

    TEST_ASSERT(s_forward_count == 1, "C->B->A forwarded once");
    TEST_ASSERT(frame_deserialize(s_forward_buf, s_forward_len, &forwarded) == UMESH_OK,
                "C->B->A forwarded frame parses");
    TEST_ASSERT(forwarded.src == 0x13, "C->B->A preserves original source");
    TEST_ASSERT(forwarded.dst == 0x11, "C->B->A preserves final destination");
    TEST_ASSERT(forwarded.link_src == 0x12, "C->B->A rewrites link source");
    TEST_ASSERT(forwarded.link_dst == 0x11, "C->B->A rewrites link destination");
    inject_raw(&s_ctx_a, &forwarded);
    TEST_ASSERT(s_rx_a == 1, "C->B->A delivers at final destination");
}

static void test_duplicate_delivery_once(void)
{
    umesh_frame_t frame;

    reset_observed();
    init_node(&s_ctx_b, 0x12, record_rx_b);
    phy_posix_set_loopback(true);

    make_frame(&frame, 0x11, 0x12, 0x11, 0x12, 0x1004, 5, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &frame);
    inject_raw(&s_ctx_b, &frame);

    TEST_ASSERT(s_rx_b == 1, "duplicate packet delivered once");
}

static void test_duplicate_forward_once(void)
{
    umesh_frame_t frame;

    reset_observed();
    init_node(&s_ctx_b, 0x12, record_rx_b);
    init_node(&s_ctx_c, 0x13, record_rx_c);
    umesh_bind_ctx(&s_ctx_b);
    routing_add(0x13, 0x13, 1, -55, 0);
    phy_posix_set_loopback(true);
    phy_set_rx_cb(capture_forward);

    make_frame(&frame, 0x11, 0x13, 0x11, 0x12, 0x1005, 5, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &frame);
    inject_raw(&s_ctx_b, &frame);

    TEST_ASSERT(s_forward_count == 1, "duplicate packet forwarded once");
}

static void test_hop_limit_stops_forwarding(void)
{
    umesh_frame_t frame;

    reset_observed();
    init_node(&s_ctx_b, 0x12, record_rx_b);
    init_node(&s_ctx_c, 0x13, record_rx_c);
    umesh_bind_ctx(&s_ctx_b);
    routing_add(0x13, 0x13, 1, -55, 1);
    phy_posix_set_loopback(true);
    phy_set_rx_cb(capture_forward);

    make_frame(&frame, 0x11, 0x13, 0x11, 0x12, 0x1006, 1, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &frame);

    TEST_ASSERT(s_forward_count == 0, "hop limit stops forwarding loop");
    TEST_ASSERT(s_ctx_b.net.last_route_result == UMESH_ERR_TOO_LONG,
                "hop limit records forwarding failure");
}

static void test_no_route_failure(void)
{
    umesh_frame_t frame;

    reset_observed();
    init_node(&s_ctx_b, 0x12, record_rx_b);
    phy_posix_set_loopback(true);
    phy_set_rx_cb(capture_forward);

    make_frame(&frame, 0x11, 0x44, 0x11, 0x12, 0x1007, 5, UMESH_CMD_PING);
    inject_raw(&s_ctx_b, &frame);

    TEST_ASSERT(s_forward_count == 0, "no route does not forward");
    TEST_ASSERT(s_ctx_b.net.last_route_result == UMESH_ERR_NOT_ROUTABLE,
                "no route records not-routable error");
}

int main(void)
{
    printf("=== test_forwarding ===\n");
    test_direct_delivery();
    test_three_node_forwarding();
    test_reverse_delivery();
    test_duplicate_delivery_once();
    test_duplicate_forward_once();
    test_hop_limit_stops_forwarding();
    test_no_route_failure();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
