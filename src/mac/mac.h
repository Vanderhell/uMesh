#ifndef UMESH_MAC_H
#define UMESH_MAC_H

#include "../common/defs.h"
#include "frame.h"

umesh_result_t mac_init(uint8_t node_id);
void           mac_set_node_id(uint8_t node_id);
umesh_result_t mac_send(umesh_frame_t *frame);
void           mac_set_rx_callback(void (*cb)(umesh_frame_t *frame,
                                              int8_t rssi));
bool           mac_channel_is_free(void);
mac_stats_t    mac_get_stats(void);
void           mac_on_raw_rx(const uint8_t *buf, uint8_t len, int8_t rssi);

#endif /* UMESH_MAC_H */
