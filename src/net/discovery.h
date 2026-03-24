#ifndef UMESH_DISCOVERY_H
#define UMESH_DISCOVERY_H

#include "../common/defs.h"
#include <stdbool.h>

umesh_result_t discovery_init(uint8_t net_id, uint8_t node_id,
                               umesh_role_t role);
void           discovery_set_auto_timing(uint32_t scan_ms,
                                          uint32_t election_ms);
void           discovery_set_local_mac(const uint8_t mac[6]);
void           discovery_set_now(uint32_t now_ms);
void           discovery_enable_gradient(bool enabled,
                                         uint32_t jitter_max_ms);
umesh_result_t discovery_join(void);
void           discovery_leave(void);
bool           discovery_is_joined(void);
uint8_t        discovery_get_node_id(void);
void           discovery_set_role(umesh_role_t role);
umesh_role_t   discovery_get_role(void);
void           discovery_set_node_id(uint8_t node_id);
bool           discovery_auto_seen_coordinator(void);
void           discovery_auto_clear_scan_flag(void);
umesh_result_t discovery_start_election(void);
umesh_result_t discovery_broadcast_election_result(void);
void           discovery_auto_clear_election_flags(void);
bool           discovery_auto_saw_lower_mac(void);
bool           discovery_auto_seen_election_result(void);
void           discovery_auto_promote_to_coordinator(void);
uint32_t       discovery_get_scan_ms(void);
uint32_t       discovery_get_election_ms(void);
void           discovery_get_local_mac(uint8_t mac[6]);
void           discovery_gradient_reset(void);
void           discovery_gradient_set_distance(uint8_t distance);
uint8_t        discovery_gradient_distance(void);
bool           discovery_gradient_ready(void);
bool           discovery_gradient_poll_rebroadcast(uint8_t *distance_out);
umesh_result_t discovery_gradient_send_beacon(uint8_t distance);
void           discovery_on_frame(const umesh_frame_t *frame, int8_t rssi);

#endif /* UMESH_DISCOVERY_H */
