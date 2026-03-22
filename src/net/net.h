#ifndef UMESH_NET_H
#define UMESH_NET_H

#include "../common/defs.h"
#include "routing.h"

umesh_result_t net_init(uint8_t net_id, uint8_t node_id,
                        umesh_role_t role);
umesh_result_t net_join(void);
void           net_leave(void);
umesh_result_t net_route(umesh_frame_t *frame);
uint8_t        net_get_node_id(void);
uint8_t        net_get_state(void);
uint8_t        net_get_node_count(void);
void           net_tick(uint32_t now_ms);
void           net_set_rx_callback(void (*cb)(const umesh_frame_t *frame,
                                               int8_t rssi));
void           net_on_frame(const umesh_frame_t *frame, int8_t rssi);

#endif /* UMESH_NET_H */
