#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/net/net.h"
#include "../src/net/routing.h"
#include "../src/net/discovery.h"
#include "../src/sec/sec.h"
#include "../src/phy/phy_hal.h"

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

static void init_posix_stack(uint8_t node_id, umesh_role_t role)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };
    phy_hal_init(&cfg);
    sec_init(TEST_KEY, 0x01, UMESH_SEC_NONE);
    net_init(0x01, node_id, role);
}

static umesh_frame_t make_gradient_beacon(uint8_t src, uint8_t distance)
{
    umesh_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.net_id = 0x01;
    frame.src = src;
    frame.dst = UMESH_ADDR_BROADCAST;
    frame.cmd = UMESH_CMD_GRADIENT_BEACON;
    frame.payload_len = 1;
    frame.payload[0] = distance;
    return frame;
}

static void test_gradient_beacon_propagation(void)
{
    uint8_t i;
    for (i = 1; i <= 4; i++) {
        umesh_frame_t frame;

        init_posix_stack((uint8_t)(0x20 + i), UMESH_ROLE_ROUTER);
        net_config_routing(UMESH_ROUTING_GRADIENT, 30000, 200);
        net_join();
        net_tick(100);

        frame = make_gradient_beacon((uint8_t)(i == 1 ? 0x01 : (0x20 + i - 1)),
                                     (uint8_t)(i - 1));
        net_on_frame(&frame, -60);

        TEST_ASSERT(net_gradient_distance() == i,
                    "gradient: beacon propagation sets distance D+1");
    }
}

static void test_gradient_uphill_selection(void)
{
    uint8_t best;

    neighbor_init();
    neighbor_update(0x22, 2, -65, 100);
    neighbor_update(0x23, 3, -40, 100);
    neighbor_update(0x24, 1, -80, 100);

    best = neighbor_find_uphill(3);
    TEST_ASSERT(best == 0x24, "gradient: chooses lowest uphill distance");

    neighbor_update(0x25, 1, -50, 101);
    best = neighbor_find_uphill(3);
    TEST_ASSERT(best == 0x25, "gradient: same distance -> prefer better RSSI");
}

static void test_gradient_no_uphill(void)
{
    uint8_t best;

    neighbor_init();
    neighbor_update(0x30, 1, -60, 100);
    neighbor_update(0x31, 2, -55, 100);

    best = neighbor_find_uphill(1);
    TEST_ASSERT(best == UMESH_ADDR_BROADCAST,
                "gradient: no uphill neighbor -> broadcast sentinel");
}

static void test_gradient_beacon_storm_prevention(void)
{
    umesh_frame_t equal_frame;
    umesh_frame_t worse_frame;
    uint8_t dist = UINT8_MAX;

    init_posix_stack(0x33, UMESH_ROLE_ROUTER);
    net_config_routing(UMESH_ROUTING_GRADIENT, 30000, 200);
    net_join();
    net_tick(100);

    discovery_gradient_set_distance(2);

    equal_frame = make_gradient_beacon(0x20, 1); /* candidate = 2 (no improvement) */
    net_on_frame(&equal_frame, -60);
    discovery_set_now(1000);
    TEST_ASSERT(!discovery_gradient_poll_rebroadcast(&dist),
                "gradient: equal distance does not rebroadcast");

    worse_frame = make_gradient_beacon(0x21, 2); /* candidate = 3 (worse) */
    net_on_frame(&worse_frame, -60);
    discovery_set_now(1200);
    TEST_ASSERT(!discovery_gradient_poll_rebroadcast(&dist),
                "gradient: worse distance does not rebroadcast");
}

static void test_gradient_neighbor_expiry(void)
{
    neighbor_init();
    neighbor_update(0x41, 2, -60, 0);
    TEST_ASSERT(neighbor_count() == 1, "gradient: neighbor inserted");
    neighbor_expire(31001);
    TEST_ASSERT(neighbor_count() == 0, "gradient: neighbor expired after 31s");
}

static void test_gradient_fallback_to_dv(void)
{
    umesh_frame_t frame;
    umesh_result_t r;

    init_posix_stack(UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR);
    net_config_routing(UMESH_ROUTING_GRADIENT, 30000, 200);
    net_join();

    routing_add(0x44, 0x44, 1, -55, 100);

    memset(&frame, 0, sizeof(frame));
    frame.net_id = 0x01;
    frame.dst = 0x44;
    frame.src = UMESH_ADDR_COORDINATOR;
    frame.cmd = UMESH_CMD_PING;
    frame.flags = UMESH_FLAG_PRIO_NORMAL;
    frame.payload_len = 0;
    frame.hop_count = UMESH_MAX_HOP_COUNT;

    r = net_route(&frame);
    TEST_ASSERT(r == UMESH_OK, "gradient: non-coordinator dst falls back to DV");
}

int main(void)
{
    printf("=== test_gradient ===\n");
    test_gradient_beacon_propagation();
    test_gradient_uphill_selection();
    test_gradient_no_uphill();
    test_gradient_beacon_storm_prevention();
    test_gradient_neighbor_expiry();
    test_gradient_fallback_to_dv();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
