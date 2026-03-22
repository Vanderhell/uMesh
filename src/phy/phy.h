#ifndef UMESH_PHY_H
#define UMESH_PHY_H

#include "../common/defs.h"
#include "phy_hal.h"

umesh_result_t phy_init(const umesh_phy_cfg_t *cfg);
umesh_result_t phy_send(const uint8_t *payload, uint8_t len);
void           phy_set_rx_cb(void (*cb)(const uint8_t *payload,
                                        uint8_t len,
                                        int8_t rssi));
void           phy_deinit(void);

#endif /* UMESH_PHY_H */
