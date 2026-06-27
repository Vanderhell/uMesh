#ifndef UMESH_PHY_HAL_H
#define UMESH_PHY_HAL_H

#include "../common/defs.h"

/* Each port implements these functions */
umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg);
umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len);
void           phy_hal_delay_ms(uint32_t duration_ms);
void           phy_hal_set_rx_cb(void (*cb)(const uint8_t *payload,
                                             uint8_t len,
                                             int8_t rssi));
void           phy_hal_deinit(void);

#endif /* UMESH_PHY_HAL_H */
