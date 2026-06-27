#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/context.h"
#include "../src/net/net.h"
#include "../src/net/discovery.h"
#include "../src/net/routing.h"
#include "../src/mac/mac.h"
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
            printf("  FAIL: %s (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

static const uint8_t TEST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static umesh_ctx_t s_coord;
static umesh_ctx_t s_node_a;
static umesh_ctx_t s_node_b;
static umesh_ctx_t s_auto;
static umesh_ctx_t s_auto2;

static uint32_t s_joined_count = 0;
static uint32_t s_left_count = 0;

static void on_node_joined(uint8_t node_id)
{
    (void)node_id;
    s_joined_count++;
}

static void on_node_left(uint8_t node_id)
{
    (void)node_id;
    s_left_count++;
}

static void init_stack(umesh_ctx_t *ctx, uint8_t node_id, umesh_role_t role,
                       const uint8_t local_mac[6], bool with_callbacks)
{
    umesh_phy_cfg_t phy_cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };

    memset(ctx, 0, sizeof(*ctx));
    umesh_bind_ctx(ctx);
    phy_hal_init(&phy_cfg);
    mac_init(node_id);
    sec_init(TEST_KEY, 0x01, UMESH_SEC_NONE);
    net_init(0x01, node_id, role);
    discovery_set_local_mac(local_mac);
    ctx->cfg.security = UMESH_SEC_NONE;
    ctx->cfg.security_epoch = 1;
    if (with_callbacks) {
        ctx->cfg.on_node_joined = on_node_joined;
        ctx->cfg.on_node_left = on_node_left;
    }
    phy_posix_set_loopback(false);
}

static void write_u32le(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void make_join_frame(umesh_frame_t *frame, const uint8_t requester_mac[6],
                            uint32_t token)
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->cmd = UMESH_CMD_JOIN;
    frame->payload_len = 15;
    memcpy(&frame->payload[0], requester_mac, 6);
    write_u32le(&frame->payload[6], token);
    write_u32le(&frame->payload[10], 1);
    frame->payload[14] = (uint8_t)UMESH_SEC_NONE;
}

static void make_assign_frame(umesh_frame_t *frame, const uint8_t requester_mac[6],
                              uint32_t token, uint8_t assigned_id,
                              const uint8_t coordinator_mac[6],
                              uint32_t term, uint32_t epoch, uint8_t sec_level)
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->cmd = UMESH_CMD_ASSIGN;
    frame->payload_len = 26;
    memcpy(&frame->payload[0], requester_mac, 6);
    write_u32le(&frame->payload[6], token);
    frame->payload[10] = assigned_id;
    memcpy(&frame->payload[11], coordinator_mac, 6);
    write_u32le(&frame->payload[17], term);
    write_u32le(&frame->payload[21], epoch);
    frame->payload[25] = sec_level;
}

static void make_election_result_frame(umesh_frame_t *frame, uint32_t term,
                                       const uint8_t winner_mac[6])
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->cmd = UMESH_CMD_ELECTION_RESULT;
    frame->payload_len = 10;
    write_u32le(&frame->payload[0], term);
    memcpy(&frame->payload[4], winner_mac, 6);
}

static umesh_active_node_t *find_active_node(umesh_ctx_t *ctx, uint8_t node_id)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        if (ctx->discovery.active_nodes[i].valid &&
            ctx->discovery.active_nodes[i].node_id == node_id) {
            return &ctx->discovery.active_nodes[i];
        }
    }
    return NULL;
}

static uint8_t count_active_nodes(umesh_ctx_t *ctx)
{
    uint8_t i;
    uint8_t count = 0;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        if (ctx->discovery.active_nodes[i].valid) {
            count++;
        }
    }
    return count;
}

static void test_two_simultaneous_joins_distinct_ids(void)
{
    umesh_frame_t join_a;
    umesh_frame_t join_b;
    const uint8_t mac_a[6] = {0x10, 0, 0, 0, 0, 1};
    const uint8_t mac_b[6] = {0x20, 0, 0, 0, 0, 2};
    umesh_active_node_t *entry_a;
    umesh_active_node_t *entry_b;
    uint8_t id_a;
    uint8_t id_b;

    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA1}, true);
    discovery_set_now(100);
    s_coord.discovery.next_assign_id = 0x02;

    make_join_frame(&join_a, mac_a, 0x1001u);
    make_join_frame(&join_b, mac_b, 0x1002u);
    umesh_bind_ctx(&s_coord);
    discovery_on_frame(&join_a, -55);
    discovery_on_frame(&join_b, -55);

    entry_a = find_active_node(&s_coord, s_coord.discovery.active_nodes[0].node_id);
    entry_b = find_active_node(&s_coord, s_coord.discovery.active_nodes[1].node_id);
    id_a = entry_a ? entry_a->node_id : 0xFF;
    id_b = entry_b ? entry_b->node_id : 0xFF;
    TEST_ASSERT(count_active_nodes(&s_coord) == 2,
                "join: simultaneous requests create two active nodes");
    TEST_ASSERT(entry_a != NULL && entry_b != NULL,
                "join: simultaneous requests record active nodes");
    TEST_ASSERT(id_a != id_b,
                "join: simultaneous requests receive distinct IDs");
    TEST_ASSERT(id_a != UMESH_ADDR_BROADCAST &&
                id_a != UMESH_ADDR_COORDINATOR &&
                id_b != UMESH_ADDR_BROADCAST &&
                id_b != UMESH_ADDR_COORDINATOR,
                "join: assigned IDs are not reserved");
}

static void test_assignment_ignored_by_other_requester(void)
{
    umesh_frame_t assign;
    const uint8_t requester_a[6] = {0x11, 0, 0, 0, 0, 1};
    const uint8_t requester_b[6] = {0x22, 0, 0, 0, 0, 2};
    const uint8_t coordinator_mac[6] = {0x01, 0, 0, 0, 0, 0};

    init_stack(&s_node_b, UMESH_ADDR_UNASSIGNED, UMESH_ROLE_ROUTER,
               requester_b, false);
    s_node_b.discovery.join_token = 0x2001u;
    make_assign_frame(&assign, requester_a, 0x2001u, 0x24,
                      coordinator_mac, 7, 1, UMESH_SEC_NONE);

    umesh_bind_ctx(&s_node_b);
    discovery_on_frame(&assign, -50);
    TEST_ASSERT(!discovery_is_joined(),
                "join: assignment intended for another requester is ignored");
    TEST_ASSERT(discovery_get_node_id() == UMESH_ADDR_UNASSIGNED,
                "join: ignored assignment does not set node id");
}

static void test_reserved_id_never_assigned(void)
{
    umesh_frame_t join;
    const uint8_t requester[6] = {0x12, 0, 0, 0, 0, 3};
    umesh_active_node_t *entry;

    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA2}, false);
    s_coord.discovery.next_assign_id = 0x01;
    discovery_set_now(200);
    make_join_frame(&join, requester, 0x3001u);
    umesh_bind_ctx(&s_coord);
    discovery_on_frame(&join, -55);

    entry = find_active_node(&s_coord, 0x02);
    TEST_ASSERT(entry != NULL, "join: reserved-slot test produced an active node");
    TEST_ASSERT(entry && entry->node_id != UMESH_ADDR_BROADCAST &&
                entry->node_id != UMESH_ADDR_COORDINATOR &&
                entry->node_id != UMESH_ADDR_UNASSIGNED,
                "join: reserved IDs are never assigned");
}

static void test_active_duplicate_id_rejected(void)
{
    umesh_frame_t join;
    const uint8_t requester[6] = {0x13, 0, 0, 0, 0, 4};

    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA3}, false);
    s_coord.discovery.next_assign_id = 0x02;
    s_coord.discovery.active_nodes[0].valid = true;
    s_coord.discovery.active_nodes[0].node_id = 0x02;
    memcpy(s_coord.discovery.active_nodes[0].node_mac, (const uint8_t[]){0xAA,0,0,0,0,1}, 6);
    s_coord.discovery.active_nodes[0].last_seen_ms = 1;
    discovery_set_now(300);
    make_join_frame(&join, requester, 0x4001u);
    umesh_bind_ctx(&s_coord);
    discovery_on_frame(&join, -55);

    TEST_ASSERT(find_active_node(&s_coord, 0x02) != NULL,
                "join: existing active id remains present");
    TEST_ASSERT(find_active_node(&s_coord, 0x03) != NULL,
                "join: duplicate active id is skipped and next free id is used");
}

static void test_address_exhaustion_returns_failure(void)
{
    umesh_frame_t join;
    uint8_t i;
    const uint8_t requester[6] = {0x14, 0, 0, 0, 0, 5};

    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA4}, false);
    s_coord.discovery.next_assign_id = 0x02;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        s_coord.discovery.active_nodes[i].valid = true;
        s_coord.discovery.active_nodes[i].node_id = (uint8_t)(0x02 + i);
        memset(s_coord.discovery.active_nodes[i].node_mac, i, 6);
        s_coord.discovery.active_nodes[i].last_seen_ms = 1;
    }
    discovery_set_now(400);
    make_join_frame(&join, requester, 0x5001u);
    umesh_bind_ctx(&s_coord);
    discovery_on_frame(&join, -55);

    TEST_ASSERT(discovery_get_last_join_result() == UMESH_ERR_NOT_ROUTABLE,
                "join: address exhaustion returns not routable");
}

static void test_expired_left_id_reuse(void)
{
    umesh_frame_t join;
    umesh_active_node_t *entry;
    const uint8_t requester[6] = {0x15, 0, 0, 0, 0, 6};

    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA5}, false);
    s_coord.discovery.next_assign_id = 0x02;
    s_coord.discovery.active_nodes[0].valid = true;
    s_coord.discovery.active_nodes[0].node_id = 0x02;
    memset(s_coord.discovery.active_nodes[0].node_mac, 0x33, 6);
    s_coord.discovery.active_nodes[0].last_seen_ms = 0;
    discovery_set_now(UMESH_NODE_TIMEOUT_MS + 1u);
    discovery_tick(UMESH_NODE_TIMEOUT_MS + 1u);

    make_join_frame(&join, requester, 0x6001u);
    umesh_bind_ctx(&s_coord);
    discovery_on_frame(&join, -55);

    entry = find_active_node(&s_coord, 0x02);
    TEST_ASSERT(entry != NULL && entry->node_id == 0x02,
                "join: expired id becomes available again");
}

static void test_on_node_joined_fires_once(void)
{
    umesh_frame_t join;
    const uint8_t requester[6] = {0x16, 0, 0, 0, 0, 7};

    s_joined_count = 0;
    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA6}, true);
    discovery_set_now(500);
    make_join_frame(&join, requester, 0x7001u);
    umesh_bind_ctx(&s_coord);
    discovery_on_frame(&join, -55);
    discovery_on_frame(&join, -55);

    TEST_ASSERT(s_joined_count == 1,
                "join: on_node_joined fires once for duplicate join traffic");
}

static void test_on_node_left_fires_once(void)
{
    s_left_count = 0;
    init_stack(&s_coord, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               (const uint8_t[]){0x02,0,0,0,0,0xA7}, true);
    s_coord.discovery.active_nodes[0].valid = true;
    s_coord.discovery.active_nodes[0].node_id = 0x22;
    memset(s_coord.discovery.active_nodes[0].node_mac, 0x44, 6);
    s_coord.discovery.active_nodes[0].last_seen_ms = 0;

    discovery_set_now(UMESH_NODE_TIMEOUT_MS + 1u);
    discovery_tick(UMESH_NODE_TIMEOUT_MS + 1u);
    discovery_tick(UMESH_NODE_TIMEOUT_MS + 2u);

    TEST_ASSERT(s_left_count == 1,
                "join: on_node_left fires once for timeout removal");
}

static void test_stale_election_result_is_ignored(void)
{
    umesh_frame_t result;
    const uint8_t low_mac[6] = {0x10, 0, 0, 0, 0, 1};
    const uint8_t high_mac[6] = {0x40, 0, 0, 0, 0, 9};

    init_stack(&s_auto, UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO,
               high_mac, false);
    s_auto.discovery.seen_election_term = 5;
    s_auto.discovery.role = UMESH_ROLE_ROUTER;
    s_auto.discovery.node_id = 0x22;
    make_election_result_frame(&result, 4, low_mac);
    umesh_bind_ctx(&s_auto);
    discovery_on_frame(&result, -55);

    TEST_ASSERT(s_auto.discovery.seen_election_term == 5,
                "election: stale result does not advance term");
    TEST_ASSERT(s_auto.discovery.role == UMESH_ROLE_ROUTER,
                "election: stale result is ignored");
}

static void test_newer_election_epoch_supersedes_old(void)
{
    umesh_frame_t result;
    const uint8_t low_mac[6] = {0x10, 0, 0, 0, 0, 1};
    const uint8_t high_mac[6] = {0x40, 0, 0, 0, 0, 9};

    init_stack(&s_auto, UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO,
               high_mac, false);
    s_auto.discovery.seen_election_term = 5;
    s_auto.discovery.role = UMESH_ROLE_ROUTER;
    s_auto.discovery.node_id = 0x22;
    make_election_result_frame(&result, 6, low_mac);
    umesh_bind_ctx(&s_auto);
    discovery_on_frame(&result, -55);

    TEST_ASSERT(s_auto.discovery.seen_election_term == 6,
                "election: newer result supersedes old term");
    TEST_ASSERT(s_auto.discovery.auto_seen_result,
                "election: newer result is accepted");
}

static void test_two_coordinators_converge_to_one_winner(void)
{
    umesh_frame_t result;
    const uint8_t low_mac[6] = {0x10, 0, 0, 0, 0, 1};
    const uint8_t high_mac[6] = {0x50, 0, 0, 0, 0, 2};

    init_stack(&s_node_a, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               low_mac, false);
    init_stack(&s_node_b, UMESH_ADDR_COORDINATOR, UMESH_ROLE_COORDINATOR,
               high_mac, false);
    s_node_a.discovery.seen_election_term = 7;
    s_node_b.discovery.seen_election_term = 7;
    make_election_result_frame(&result, 7, low_mac);

    umesh_bind_ctx(&s_node_b);
    discovery_on_frame(&result, -55);
    umesh_bind_ctx(&s_node_a);
    discovery_on_frame(&result, -55);

    TEST_ASSERT(s_node_b.discovery.role == UMESH_ROLE_ROUTER,
                "election: higher coordinator converges to router");
    TEST_ASSERT(s_node_a.discovery.role == UMESH_ROLE_COORDINATOR,
                "election: lower coordinator remains winner");
}

static void test_coordinator_loss_starts_election_after_timeout(void)
{
    const uint8_t local_mac[6] = {0x30, 0, 0, 0, 0, 3};

    init_stack(&s_auto2, UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO,
               local_mac, false);
    net_config_auto(10, 20, local_mac);
    net_join();
    s_auto2.net.state = UMESH_STATE_CONNECTED;
    s_auto2.net.role = UMESH_ROLE_ROUTER;
    s_auto2.net.role_cfg = UMESH_ROLE_AUTO;
    s_auto2.net.last_coord_seen_ms = 0;
    s_auto2.net.state_enter_ms = 0;

    net_tick(UMESH_NODE_TIMEOUT_MS - 1u);
    TEST_ASSERT(net_get_state() == UMESH_STATE_CONNECTED,
                "election: node stays connected before coordinator timeout");

    net_tick(UMESH_NODE_TIMEOUT_MS + 1u);
    TEST_ASSERT(net_get_state() == UMESH_STATE_SCANNING,
                "election: coordinator timeout moves node to scanning");

    net_tick(UMESH_NODE_TIMEOUT_MS + 12u);
    TEST_ASSERT(net_get_state() == UMESH_STATE_ELECTION,
                "election: scan timeout starts election after coordinator loss");
}

static void test_auto_node_does_not_self_elect_before_timeout(void)
{
    const uint8_t local_mac[6] = {0x31, 0, 0, 0, 0, 4};

    init_stack(&s_auto2, UMESH_ADDR_UNASSIGNED, UMESH_ROLE_AUTO,
               local_mac, false);
    net_config_auto(50, 100, local_mac);
    net_join();

    net_tick(49);
    TEST_ASSERT(net_get_state() == UMESH_STATE_SCANNING,
                "election: AUTO node stays scanning before timeout");
    TEST_ASSERT(net_get_role() != UMESH_ROLE_COORDINATOR,
                "election: AUTO node does not self-elect early");

    net_tick(50);
    TEST_ASSERT(net_get_state() == UMESH_STATE_ELECTION,
                "election: AUTO node enters election only at timeout");
}

int main(void)
{
    printf("=== test_election ===\n");
    test_two_simultaneous_joins_distinct_ids();
    test_assignment_ignored_by_other_requester();
    test_reserved_id_never_assigned();
    test_active_duplicate_id_rejected();
    test_address_exhaustion_returns_failure();
    test_expired_left_id_reuse();
    test_on_node_joined_fires_once();
    test_on_node_left_fires_once();
    test_stale_election_result_is_ignored();
    test_newer_election_epoch_supersedes_old();
    test_two_coordinators_converge_to_one_winner();
    test_coordinator_loss_starts_election_after_timeout();
    test_auto_node_does_not_self_elect_before_timeout();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
