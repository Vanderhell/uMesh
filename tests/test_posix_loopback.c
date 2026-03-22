#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/phy/phy_hal.h"
#include "../port/posix/phy_posix.h"

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

static uint8_t  s_rx_buf[256];
static uint8_t  s_rx_len = 0;
static int8_t   s_rx_rssi = 0;
static int      s_rx_count = 0;

static void rx_cb(const uint8_t *payload, uint8_t len, int8_t rssi)
{
    s_rx_len  = len;
    s_rx_rssi = rssi;
    s_rx_count++;
    if (len <= 255) {
        memcpy(s_rx_buf, payload, len);
    }
}

static void test_loopback_basic(void)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };
    const uint8_t  data[] = { 0x01, 0x02, 0x03, 0xAA, 0xBB };
    umesh_result_t r;

    phy_hal_init(&cfg);
    phy_hal_set_rx_cb(rx_cb);
    s_rx_count = 0;

    r = phy_hal_send(data, sizeof(data));
    TEST_ASSERT(r == UMESH_OK, "loopback: send returns OK");
    TEST_ASSERT(s_rx_count == 1, "loopback: RX callback called once");
    TEST_ASSERT(s_rx_len == sizeof(data), "loopback: received length matches");
    TEST_ASSERT(memcmp(s_rx_buf, data, sizeof(data)) == 0,
                "loopback: received data matches sent data");
}

static void test_loopback_rssi(void)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };
    const uint8_t  data[] = { 0xFF };
    s_rx_rssi = 0;

    phy_hal_init(&cfg);
    phy_hal_set_rx_cb(rx_cb);

    phy_hal_send(data, sizeof(data));
    TEST_ASSERT(s_rx_rssi == -60, "loopback: simulated RSSI == -60 dBm");
}

static void test_loopback_multiple(void)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };
    int i;
    uint8_t pkt[4];

    phy_hal_init(&cfg);
    phy_hal_set_rx_cb(rx_cb);
    s_rx_count = 0;

    for (i = 0; i < 5; i++) {
        pkt[0] = (uint8_t)i;
        pkt[1] = (uint8_t)(i + 1);
        pkt[2] = (uint8_t)(i + 2);
        pkt[3] = (uint8_t)(i + 3);
        phy_hal_send(pkt, 4);
    }
    TEST_ASSERT(s_rx_count == 5, "loopback: 5 sends = 5 RX callbacks");
    /* Last packet */
    TEST_ASSERT(s_rx_buf[0] == 4, "loopback: last packet data[0] correct");
}

static void test_loopback_deinit(void)
{
    umesh_phy_cfg_t cfg = { .channel = 6, .tx_power = 60, .net_id = 0x01 };
    const uint8_t data[] = { 0x42 };

    phy_hal_init(&cfg);
    phy_hal_set_rx_cb(rx_cb);
    s_rx_count = 0;

    phy_hal_deinit();

    /* After deinit, callback is unregistered, no RX should happen */
    phy_hal_send(data, sizeof(data));
    TEST_ASSERT(s_rx_count == 0, "loopback: no RX after deinit");
}

int main(void)
{
    printf("=== test_posix_loopback ===\n");
    test_loopback_basic();
    test_loopback_rssi();
    test_loopback_multiple();
    test_loopback_deinit();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
