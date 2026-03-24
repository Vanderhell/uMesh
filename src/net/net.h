#ifndef UMESH_NET_H
#define UMESH_NET_H

#include "../common/defs.h"
#include "routing.h"

umesh_result_t net_init(uint8_t net_id, uint8_t node_id,
                        umesh_role_t role);
void           net_config_auto(uint32_t scan_ms,
                               uint32_t election_ms,
                               const uint8_t local_mac[6]);
void           net_config_routing(umesh_routing_mode_t routing_mode,
                                  uint32_t gradient_beacon_ms,
                                  uint32_t gradient_jitter_max_ms);
void           net_config_power(umesh_power_mode_t power_mode,
                                uint32_t light_interval_ms,
                                uint32_t light_listen_window_ms);
umesh_result_t net_join(void);
void           net_leave(void);
umesh_result_t net_route(umesh_frame_t *frame);
uint8_t        net_get_node_id(void);
uint8_t        net_get_state(void);
umesh_role_t   net_get_role(void);
bool           net_is_coordinator(void);
umesh_result_t net_trigger_election(void);
uint8_t        net_gradient_distance(void);
umesh_routing_mode_t net_get_routing_mode(void);
umesh_result_t net_gradient_refresh(void);
uint8_t        net_get_neighbor_count(void);
bool           net_get_neighbor(uint8_t index, umesh_neighbor_t *out);
uint8_t        net_get_node_count(void);
void           net_tick(uint32_t now_ms);
void           net_set_rx_callback(void (*cb)(const umesh_frame_t *frame,
                                               int8_t rssi));
void           net_on_frame(const umesh_frame_t *frame, int8_t rssi);

#endif /* UMESH_NET_H */
