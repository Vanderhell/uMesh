#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/mac/mac.h"
#include "../src/mac/frame.h"
#include "../src/phy/phy_hal.h"
#include "../port/posix/phy_posix.h"
#include "../src/phy/phy.h"

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

/* Track received frames */
static umesh_frame_t s_last_rx_frame;
static int8_t        s_last_rx_rssi = 0;
static int           s_rx_count     = 0;

static void on_mac_rx(umesh_frame_t *frame, int8_t rssi)
{
    s_last_rx_frame = *frame;
    s_last_rx_rssi  = rssi;
    s_rx_count++;
}

static void init_posix_phy(void)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };
    phy_hal_init(&cfg);
}

static void test_mac_send_broadcast(void)
{
    umesh_frame_t frame;

    init_posix_phy();
    mac_init(0x01);
    mac_set_rx_callback(on_mac_rx);
    s_rx_count = 0;

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = 0x01;
    frame.dst         = UMESH_ADDR_BROADCAST;
    frame.src         = 0x01;
    frame.flags       = UMESH_FLAG_PRIO_NORMAL; /* no ACK_REQ for broadcast */
    frame.cmd         = UMESH_CMD_PING;
    frame.payload_len = 0;
    frame.seq_num     = 0x0001;
    frame.hop_count   = 1;

    umesh_result_t r = mac_send(&frame);
    TEST_ASSERT(r == UMESH_OK, "broadcast: send returns OK");
    /* In loopback we receive our own broadcast */
    TEST_ASSERT(s_rx_count == 1, "broadcast: received via loopback");
    TEST_ASSERT(s_last_rx_frame.dst == UMESH_ADDR_BROADCAST,
                "broadcast: dst == 0x00");
}

static void test_mac_send_unicast_no_ack(void)
{
    umesh_frame_t frame;

    init_posix_phy();
    mac_init(0x01);
    mac_set_rx_callback(on_mac_rx);
    s_rx_count = 0;

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = 0x01;
    frame.dst         = 0x02;
    frame.src         = 0x01;
    frame.flags       = UMESH_FLAG_PRIO_NORMAL; /* no ACK_REQ */
    frame.cmd         = UMESH_CMD_SENSOR_TEMP;
    frame.payload_len = 4;
    frame.payload[0]  = 0xDE;
    frame.payload[1]  = 0xAD;
    frame.payload[2]  = 0xBE;
    frame.payload[3]  = 0xEF;
    frame.seq_num     = 0x0002;
    frame.hop_count   = 1;

    umesh_result_t r = mac_send(&frame);
    TEST_ASSERT(r == UMESH_OK, "unicast-no-ack: send OK (fire and forget)");
}

static void test_mac_stats_tx(void)
{
    umesh_frame_t frame;
    mac_stats_t   stats;

    init_posix_phy();
    mac_init(0x02);
    mac_set_rx_callback(on_mac_rx);

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = 0x01;
    frame.dst         = UMESH_ADDR_BROADCAST;
    frame.src         = 0x02;
    frame.flags       = 0;
    frame.cmd         = UMESH_CMD_PING;
    frame.seq_num     = 0x0010;
    frame.payload_len = 0;
    frame.hop_count   = 1;

    mac_send(&frame);
    mac_send(&frame);
    mac_send(&frame);

    stats = mac_get_stats();
    TEST_ASSERT(stats.tx_count >= 3, "stats: tx_count >= 3");
}

static void test_mac_channel_free(void)
{
    init_posix_phy();
    mac_init(0x01);
    /* In POSIX simulation, channel is always free (RSSI = -100 < -85) */
    TEST_ASSERT(mac_channel_is_free(), "cca: channel is free on init");
}

int main(void)
{
    printf("=== test_mac ===\n");
    test_mac_send_broadcast();
    test_mac_send_unicast_no_ack();
    test_mac_stats_tx();
    test_mac_channel_free();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
