#include "../../src/phy/phy_hal.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/*
 * POSIX PHY simulation for PC testing.
 * TX: stores frame in shared loopback buffer.
 * RX: reads from buffer, calls callback.
 * Simulated RSSI: constant -60 dBm.
 */

#define POSIX_LOOPBACK_BUF_SIZE  256
#define POSIX_SIMULATED_RSSI     (-90)   /* below CCA threshold (-85) = channel free */

static void (*s_rx_cb)(const uint8_t*, uint8_t, int8_t) = NULL;
static void (*s_delay_hook)(uint32_t) = NULL;
static uint8_t  s_loopback_buf[POSIX_LOOPBACK_BUF_SIZE];
static uint8_t  s_loopback_len = 0;
static bool     s_loopback_pending = false;
static bool     s_loopback_enabled = true;

umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg)
{
    (void)cfg;
    s_rx_cb          = NULL;
    s_loopback_len   = 0;
    s_loopback_pending = false;
    s_loopback_enabled = true;
    s_delay_hook = NULL;
    return UMESH_OK;
}

umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len)
{
    if (!payload || len == 0) return UMESH_ERR_NULL_PTR;
    if ((size_t)len > sizeof(s_loopback_buf)) return UMESH_ERR_TOO_LONG;

    /* Store in loopback buffer */
    memcpy(s_loopback_buf, payload, len);
    s_loopback_len     = len;
    s_loopback_pending = true;

    /* Immediately deliver to RX callback (loopback) */
    if (s_loopback_enabled && s_rx_cb && s_loopback_pending) {
        s_loopback_pending = false;
        s_rx_cb(s_loopback_buf, s_loopback_len,
                (int8_t)POSIX_SIMULATED_RSSI);
    }

    return UMESH_OK;
}

void phy_hal_set_rx_cb(void (*cb)(const uint8_t *payload,
                                   uint8_t len,
                                   int8_t rssi))
{
    s_rx_cb = cb;
}

void phy_hal_deinit(void)
{
    s_rx_cb          = NULL;
    s_loopback_pending = false;
    s_delay_hook = NULL;
}

void phy_hal_delay_ms(uint32_t duration_ms)
{
    if (s_delay_hook) {
        s_delay_hook(duration_ms);
        return;
    }
#ifdef _WIN32
    Sleep(duration_ms);
#else
    usleep(duration_ms * 1000u);
#endif
}

/*
 * phy_posix_set_loopback() — test helper.
 * When loopback is disabled, TX frames are buffered but not
 * automatically delivered. Call phy_posix_flush() to deliver them.
 */
void phy_posix_set_loopback(bool enabled)
{
    s_loopback_enabled = enabled;
}

void phy_posix_flush(void)
{
    if (s_loopback_pending && s_rx_cb) {
        s_loopback_pending = false;
        s_rx_cb(s_loopback_buf, s_loopback_len,
                (int8_t)POSIX_SIMULATED_RSSI);
    }
}

void phy_posix_set_delay_hook(void (*hook)(uint32_t duration_ms))
{
    s_delay_hook = hook;
}
