#ifndef UMESH_PHY_HAL_H
#define UMESH_PHY_HAL_H

#include "../common/defs.h"

typedef struct {
    uint8_t channel;
    uint8_t tx_power;
    uint8_t net_id;
} umesh_phy_cfg_t;

/* Each port implements these functions */
umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg);
umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len);
void           phy_hal_set_rx_cb(void (*cb)(const uint8_t *payload,
                                             uint8_t len,
                                             int8_t rssi));
void           phy_hal_deinit(void);

#endif /* UMESH_PHY_HAL_H */
