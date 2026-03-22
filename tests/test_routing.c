#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/net/routing.h"

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

static void test_routing_add_find(void)
{
    umesh_route_entry_t out;

    routing_init();

    TEST_ASSERT(!routing_find(0x02, &out), "add/find: empty table -> not found");

    TEST_ASSERT(routing_add(0x02, 0x01, 1, -55, 1000), "add/find: add route");
    TEST_ASSERT(routing_find(0x02, &out), "add/find: find added route");
    TEST_ASSERT(out.dst_node  == 0x02, "add/find: dst_node correct");
    TEST_ASSERT(out.next_hop  == 0x01, "add/find: next_hop correct");
    TEST_ASSERT(out.hop_count == 1,    "add/find: hop_count correct");
    TEST_ASSERT(out.last_rssi == -55,  "add/find: rssi correct");
}

static void test_routing_metric(void)
{
    /* Excellent RSSI: metric = 1*10 + 0 = 10 */
    TEST_ASSERT(routing_metric(1, -40) == 10, "metric: 1 hop, excellent RSSI = 10");
    /* Good RSSI:      metric = 1*10 + 2 = 12 */
    TEST_ASSERT(routing_metric(1, -60) == 12, "metric: 1 hop, good RSSI = 12");
    /* Fair RSSI:      metric = 1*10 + 5 = 15 */
    TEST_ASSERT(routing_metric(1, -80) == 15, "metric: 1 hop, fair RSSI = 15");
    /* Poor RSSI:      metric = 2*10 + 10 = 30 */
    TEST_ASSERT(routing_metric(2, -90) == 30, "metric: 2 hops, poor RSSI = 30");
    /* Better 2-hop than bad 1-hop: 2*10+2=22 < 1*10+10=20? No: 22 > 20 */
    TEST_ASSERT(routing_metric(1, -90) == 20, "metric: 1 hop, poor RSSI = 20");
    TEST_ASSERT(routing_metric(2, -40) == 20, "metric: 2 hops, excellent RSSI = 20");
}

static void test_routing_best_route_kept(void)
{
    umesh_route_entry_t out;

    routing_init();
    routing_add(0x03, 0x01, 1, -55, 1000); /* metric = 12 */
    routing_add(0x03, 0x02, 2, -40, 2000); /* metric = 20 — worse */

    routing_find(0x03, &out);
    TEST_ASSERT(out.next_hop == 0x01, "best-route: lower metric kept (via 0x01)");

    /* Now add a better route */
    routing_add(0x03, 0x04, 1, -45, 3000); /* metric = 10 — better */
    routing_find(0x03, &out);
    TEST_ASSERT(out.next_hop == 0x04, "best-route: better metric replaces (via 0x04)");
}

static void test_routing_expire(void)
{
    umesh_route_entry_t out;

    routing_init();
    routing_add(0x05, 0x01, 1, -55, 0);

    /* Not yet expired */
    routing_expire(UMESH_NODE_TIMEOUT_MS - 1);
    TEST_ASSERT(routing_find(0x05, &out), "expire: route still valid before timeout");

    /* Expired */
    routing_expire(UMESH_NODE_TIMEOUT_MS + 1);
    TEST_ASSERT(!routing_find(0x05, &out), "expire: route removed after timeout");
}

static void test_routing_remove(void)
{
    umesh_route_entry_t out;

    routing_init();
    routing_add(0x06, 0x01, 1, -55, 1000);
    TEST_ASSERT(routing_find(0x06, &out), "remove: route present");

    routing_remove(0x06);
    TEST_ASSERT(!routing_find(0x06, &out), "remove: route gone after remove");
}

static void test_routing_table_full(void)
{
    uint8_t i;
    routing_init();

    /* Fill table (max 16 routes: dst 0x02 to 0x11) */
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        routing_add((uint8_t)(0x02 + i), 0x01, 1, -55, 1000);
    }

    /* Next add should fail */
    TEST_ASSERT(!routing_add(0x20, 0x01, 1, -55, 1000),
                "table-full: add fails when table full");
}

int main(void)
{
    printf("=== test_routing ===\n");
    test_routing_add_find();
    test_routing_metric();
    test_routing_best_route_kept();
    test_routing_expire();
    test_routing_remove();
    test_routing_table_full();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
