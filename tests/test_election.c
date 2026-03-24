#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/net/net.h"
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

static void test_election_lowest_mac_wins(void)
{
    uint8_t high_mac[6] = {0x30, 0, 0, 0, 0, 1};
    uint8_t low_mac[6] = {0x10, 0, 0, 0, 0, 1};
    umesh_frame_t frame;

    init_posix_stack(UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO);
    net_config_auto(10, 20, high_mac);
    net_join();

    net_tick(0);
    net_tick(11); /* SCANNING -> ELECTION */

    memset(&frame, 0, sizeof(frame));
    frame.net_id = 0x01;
    frame.src = 0x44;
    frame.dst = UMESH_ADDR_BROADCAST;
    frame.cmd = UMESH_CMD_ELECTION;
    frame.payload_len = 6;
    memcpy(frame.payload, low_mac, 6);
    net_on_frame(&frame, -60);

    net_tick(35);
    TEST_ASSERT(net_get_role() == UMESH_ROLE_ROUTER,
                "election: lower MAC wins, node stays router");
}

static void test_election_single_node(void)
{
    uint8_t mac[6] = {0x20, 0, 0, 0, 0, 1};

    init_posix_stack(UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO);
    net_config_auto(10, 20, mac);
    net_join();

    net_tick(0);
    net_tick(11);
    net_tick(35);

    TEST_ASSERT(net_get_role() == UMESH_ROLE_COORDINATOR,
                "election: single node elects self coordinator");
    TEST_ASSERT(net_get_node_id() == UMESH_ADDR_COORDINATOR,
                "election: self-elected coordinator id is 0x01");
}

static void test_election_coordinator_failover(void)
{
    uint8_t mac[6] = {0x40, 0, 0, 0, 0, 2};
    umesh_frame_t frame;
    uint8_t assigned = 0x22;

    init_posix_stack(UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO);
    net_config_auto(10, 20, mac);
    net_join();

    net_tick(0);
    net_tick(11); /* starts election */

    memset(&frame, 0, sizeof(frame));
    frame.net_id = 0x01;
    frame.src = UMESH_ADDR_COORDINATOR;
    frame.dst = UMESH_ADDR_BROADCAST;
    frame.cmd = UMESH_CMD_ELECTION_RESULT;
    frame.payload_len = 6;
    frame.payload[0] = 0x10; /* winner lower than local */
    net_on_frame(&frame, -60);
    net_tick(20); /* move to JOINING */

    memset(&frame, 0, sizeof(frame));
    frame.net_id = 0x01;
    frame.src = UMESH_ADDR_COORDINATOR;
    frame.dst = UMESH_ADDR_BROADCAST;
    frame.cmd = UMESH_CMD_ASSIGN;
    frame.payload_len = 1;
    frame.payload[0] = assigned;
    net_on_frame(&frame, -50);
    net_tick(21);

    TEST_ASSERT(net_get_state() == UMESH_STATE_CONNECTED,
                "failover: node joins as connected router");
    TEST_ASSERT(net_get_role() == UMESH_ROLE_ROUTER,
                "failover: node role is router before timeout");

    net_tick(21 + UMESH_NODE_TIMEOUT_MS + 5);
    TEST_ASSERT(net_get_state() == UMESH_STATE_SCANNING,
                "failover: coordinator timeout starts re-election");
}

static void test_role_auto_backward_compat(void)
{
    init_posix_stack(UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR);
    TEST_ASSERT(net_join() == UMESH_OK, "compat: explicit coordinator join OK");
    TEST_ASSERT(net_get_role() == UMESH_ROLE_COORDINATOR,
                "compat: explicit coordinator role unchanged");
    TEST_ASSERT(net_get_state() == UMESH_STATE_CONNECTED,
                "compat: explicit coordinator skips election");
}

int main(void)
{
    printf("=== test_election ===\n");
    test_election_lowest_mac_wins();
    test_election_single_node();
    test_election_coordinator_failover();
    test_role_auto_backward_compat();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
