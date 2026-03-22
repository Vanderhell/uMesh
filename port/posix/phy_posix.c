#include "../../src/phy/phy_hal.h"
#include <string.h>

/* Stub — will be implemented in Step 8 */
static void (*s_rx_cb)(const uint8_t*, uint8_t, int8_t) = NULL;

umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg)
{
    (void)cfg;
    return UMESH_OK;
}

umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len)
{
    (void)payload;
    (void)len;
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
    s_rx_cb = NULL;
}
