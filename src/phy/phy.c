#include "phy.h"
#include "phy_hal.h"

umesh_result_t phy_init(const umesh_phy_cfg_t *cfg)
{
    return phy_hal_init(cfg);
}

umesh_result_t phy_send(const uint8_t *payload, uint8_t len)
{
    return phy_hal_send(payload, len);
}

void phy_set_rx_cb(void (*cb)(const uint8_t *payload, uint8_t len, int8_t rssi))
{
    phy_hal_set_rx_cb(cb);
}

void phy_deinit(void)
{
    phy_hal_deinit();
}
