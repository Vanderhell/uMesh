#include "net.h"
#include "discovery.h"
#include "routing.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#include "../sec/sec.h"
#include <string.h>

static uint8_t       s_node_id = 0;
static uint8_t       s_net_id = 0;
static umesh_role_t  s_role = UMESH_ROLE_END_NODE;
static umesh_role_t  s_role_cfg = UMESH_ROLE_END_NODE;
static umesh_state_t s_state = UMESH_STATE_UNINIT;
static uint16_t      s_seq_num = 0;

static uint32_t      s_last_route_update_ms = 0;
static uint32_t      s_last_coord_seen_ms = 0;
static uint32_t      s_last_join_ms = 0;
static uint32_t      s_state_enter_ms = 0;
static uint32_t      s_now_ms = 0;
static uint32_t      s_scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
static uint32_t      s_election_ms = UMESH_ELECTION_TIMEOUT_MS;
static umesh_routing_mode_t s_routing_mode = UMESH_ROUTING_DISTANCE_VECTOR;
static uint32_t      s_gradient_beacon_ms = UMESH_GRADIENT_BEACON_MS;
static uint32_t      s_gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;
static uint32_t      s_last_gradient_beacon_ms = 0;

static void (*s_net_rx_cb)(const umesh_frame_t *frame, int8_t rssi) = NULL;

#define UMESH_JOIN_RETRY_MS 1000u

static uint16_t next_seq(void)
{
    s_seq_num = (uint16_t)((s_seq_num + 1u) & 0x0FFF);
    if (s_seq_num == 0) {
        sec_regenerate_salt();
        s_seq_num = 1;
    }
    return s_seq_num;
}

static umesh_result_t net_route_distance_vector(umesh_frame_t *frame)
{
    umesh_route_entry_t route;

    if (frame->dst == UMESH_ADDR_BROADCAST) {
        return mac_send(frame);
    }

    if (!routing_find(frame->dst, &route)) {
        if (!routing_find(UMESH_ADDR_COORDINATOR, &route)) {
            return UMESH_ERR_NOT_ROUTABLE;
        }
    }

    return mac_send(frame);
}

static void enter_scanning(void)
{
    s_role = UMESH_ROLE_ROUTER;
    discovery_set_role(UMESH_ROLE_ROUTER);
    discovery_set_node_id(UMESH_ADDR_UNASSIGNED);
    s_node_id = UMESH_ADDR_UNASSIGNED;
    mac_set_node_id(UMESH_ADDR_UNASSIGNED);
    discovery_auto_clear_scan_flag();
    discovery_auto_clear_election_flags();
    discovery_gradient_reset();
    s_state = UMESH_STATE_SCANNING;
    s_state_enter_ms = s_now_ms;
    s_last_coord_seen_ms = s_now_ms;
}

static void on_mac_rx(umesh_frame_t *frame, int8_t rssi)
{
    if (!frame) return;
    if (frame->net_id != s_net_id) return;

    if (frame->src == UMESH_ADDR_COORDINATOR) {
        s_last_coord_seen_ms = s_now_ms;
    }

    discovery_set_now(s_now_ms);
    discovery_on_frame(frame, rssi);

    if (s_state == UMESH_STATE_JOINING) {
        uint8_t assigned = discovery_get_node_id();
        if (assigned != UMESH_ADDR_UNASSIGNED) {
            s_node_id = assigned;
            s_state = UMESH_STATE_CONNECTED;
            s_role = discovery_get_role();
            mac_set_node_id(assigned);
            s_last_coord_seen_ms = s_now_ms;
        }
    }

    if (s_net_rx_cb) {
        s_net_rx_cb(frame, rssi);
    }
}

umesh_result_t net_init(uint8_t net_id, uint8_t node_id, umesh_role_t role)
{
    s_net_id = net_id;
    s_node_id = node_id;
    s_role_cfg = role;
    s_role = (role == UMESH_ROLE_AUTO) ? UMESH_ROLE_ROUTER : role;
    s_state = UMESH_STATE_SCANNING;
    s_seq_num = 0;
    s_last_route_update_ms = 0;
    s_last_coord_seen_ms = 0;
    s_last_join_ms = 0;
    s_state_enter_ms = 0;
    s_now_ms = 0;
    s_routing_mode = UMESH_ROUTING_DISTANCE_VECTOR;
    s_gradient_beacon_ms = UMESH_GRADIENT_BEACON_MS;
    s_gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;
    s_last_gradient_beacon_ms = 0;

    routing_init();
    mac_set_rx_callback(on_mac_rx);

    discovery_init(net_id, node_id, role);
    discovery_set_auto_timing(s_scan_ms, s_election_ms);
    discovery_enable_gradient(false, s_gradient_jitter_max_ms);

    return UMESH_OK;
}

void net_config_auto(uint32_t scan_ms, uint32_t election_ms,
                     const uint8_t local_mac[6])
{
    s_scan_ms = (scan_ms == 0) ? UMESH_DISCOVER_TIMEOUT_MS : scan_ms;
    s_election_ms = (election_ms == 0) ? UMESH_ELECTION_TIMEOUT_MS : election_ms;
    discovery_set_auto_timing(s_scan_ms, s_election_ms);
    if (local_mac) {
        discovery_set_local_mac(local_mac);
    }
}

void net_config_routing(umesh_routing_mode_t routing_mode,
                        uint32_t gradient_beacon_ms,
                        uint32_t gradient_jitter_max_ms)
{
    s_routing_mode = routing_mode;
    s_gradient_beacon_ms = (gradient_beacon_ms == 0)
        ? UMESH_GRADIENT_BEACON_MS : gradient_beacon_ms;
    s_gradient_jitter_max_ms = (gradient_jitter_max_ms == 0)
        ? UMESH_GRADIENT_JITTER_MAX_MS : gradient_jitter_max_ms;

    discovery_enable_gradient(s_routing_mode == UMESH_ROUTING_GRADIENT,
                              s_gradient_jitter_max_ms);
    if (s_routing_mode != UMESH_ROUTING_GRADIENT) {
        discovery_gradient_reset();
    }
}

umesh_result_t net_join(void)
{
    if (s_state == UMESH_STATE_UNINIT) return UMESH_ERR_NOT_INIT;

    if (s_role_cfg != UMESH_ROLE_AUTO) {
        if (s_role == UMESH_ROLE_COORDINATOR) {
            s_state = UMESH_STATE_CONNECTED;
            s_node_id = UMESH_ADDR_COORDINATOR;
            mac_set_node_id(UMESH_ADDR_COORDINATOR);
            if (s_routing_mode == UMESH_ROUTING_GRADIENT) {
                discovery_gradient_set_distance(0);
                discovery_gradient_send_beacon(0);
                s_last_gradient_beacon_ms = s_now_ms;
            }
            return UMESH_OK;
        }

        if (s_node_id != UMESH_ADDR_UNASSIGNED) {
            s_state = UMESH_STATE_CONNECTED;
            s_role = s_role_cfg;
            mac_set_node_id(s_node_id);
            routing_add(UMESH_ADDR_COORDINATOR, UMESH_ADDR_COORDINATOR,
                        1, UMESH_RSSI_GOOD, 0);
            if (s_routing_mode == UMESH_ROUTING_GRADIENT &&
                s_role == UMESH_ROLE_COORDINATOR) {
                discovery_gradient_set_distance(0);
            }
            s_last_route_update_ms =
                (uint32_t)(0u - (UMESH_ROUTE_UPDATE_MS - 1000u));
            s_last_coord_seen_ms = s_now_ms;
            return UMESH_OK;
        }

        s_state = UMESH_STATE_JOINING;
        s_last_join_ms = s_now_ms;
        return discovery_join();
    }

    enter_scanning();
    return UMESH_OK;
}

umesh_result_t net_trigger_election(void)
{
    if (s_state == UMESH_STATE_UNINIT) return UMESH_ERR_NOT_INIT;
    s_role_cfg = UMESH_ROLE_AUTO;
    enter_scanning();
    return UMESH_OK;
}

uint8_t net_gradient_distance(void)
{
    return discovery_gradient_distance();
}

umesh_routing_mode_t net_get_routing_mode(void)
{
    return s_routing_mode;
}

umesh_result_t net_gradient_refresh(void)
{
    if (s_routing_mode != UMESH_ROUTING_GRADIENT) {
        return UMESH_ERR_NOT_ROUTABLE;
    }
    if (s_state != UMESH_STATE_CONNECTED) {
        return UMESH_ERR_NOT_JOINED;
    }
    if (s_role != UMESH_ROLE_COORDINATOR) {
        return UMESH_ERR_INVALID_DST;
    }

    discovery_gradient_set_distance(0);
    s_last_gradient_beacon_ms = s_now_ms;
    return discovery_gradient_send_beacon(0);
}

uint8_t net_get_neighbor_count(void)
{
    return neighbor_count();
}

bool net_get_neighbor(uint8_t index, umesh_neighbor_t *out)
{
    return neighbor_get(index, out);
}

void net_leave(void)
{
    discovery_leave();
    discovery_gradient_reset();
    s_state = UMESH_STATE_DISCONNECTED;
    s_node_id = UMESH_ADDR_UNASSIGNED;
}

umesh_result_t net_route(umesh_frame_t *frame)
{
    if (!frame) return UMESH_ERR_NULL_PTR;
    if (s_state != UMESH_STATE_CONNECTED) return UMESH_ERR_NOT_JOINED;

    frame->hop_count = UMESH_MAX_HOP_COUNT;
    frame->src = s_node_id;
    frame->seq_num = next_seq();

    if (s_routing_mode == UMESH_ROUTING_GRADIENT &&
        frame->dst == UMESH_ADDR_COORDINATOR) {
        uint8_t my_distance = discovery_gradient_distance();
        uint8_t next_hop;

        if (s_node_id == UMESH_ADDR_COORDINATOR) {
            return UMESH_OK;
        }

        next_hop = neighbor_find_uphill(my_distance);
        if (next_hop == UMESH_ADDR_BROADCAST) {
            return UMESH_ERR_NOT_ROUTABLE;
        }
        frame->dst = next_hop;
    } else {
        return net_route_distance_vector(frame);
    }

    return mac_send(frame);
}

uint8_t net_get_node_id(void)
{
    return s_node_id;
}

uint8_t net_get_state(void)
{
    return (uint8_t)s_state;
}

umesh_role_t net_get_role(void)
{
    return s_role;
}

bool net_is_coordinator(void)
{
    return s_role == UMESH_ROLE_COORDINATOR;
}

uint8_t net_get_node_count(void)
{
    uint8_t i;
    uint8_t count = 0;
    umesh_route_entry_t dummy;
    for (i = 2; i <= 0xFD; i++) {
        if (routing_find(i, &dummy)) count++;
    }
    return count;
}

void net_set_rx_callback(void (*cb)(const umesh_frame_t *frame, int8_t rssi))
{
    s_net_rx_cb = cb;
}

void net_tick(uint32_t now_ms)
{
    uint8_t rebroadcast_distance = UINT8_MAX;

    s_now_ms = now_ms;
    routing_expire(now_ms);
    discovery_set_now(now_ms);
    if (s_routing_mode == UMESH_ROUTING_GRADIENT) {
        neighbor_expire(now_ms);
    }

    if (s_role_cfg == UMESH_ROLE_AUTO) {
        if (s_state == UMESH_STATE_SCANNING) {
            if (discovery_auto_seen_coordinator()) {
                discovery_auto_clear_scan_flag();
                s_role = UMESH_ROLE_ROUTER;
                discovery_set_role(UMESH_ROLE_ROUTER);
                s_state = UMESH_STATE_JOINING;
                s_state_enter_ms = now_ms;
                s_last_join_ms = now_ms;
                discovery_join();
            } else if (now_ms - s_state_enter_ms >= s_scan_ms) {
                s_state = UMESH_STATE_ELECTION;
                s_state_enter_ms = now_ms;
                discovery_auto_clear_election_flags();
                discovery_start_election();
            }
        } else if (s_state == UMESH_STATE_ELECTION) {
            if (discovery_auto_seen_election_result() ||
                discovery_auto_saw_lower_mac()) {
                s_role = UMESH_ROLE_ROUTER;
                discovery_set_role(UMESH_ROLE_ROUTER);
                s_state = UMESH_STATE_JOINING;
                s_state_enter_ms = now_ms;
                s_last_join_ms = now_ms;
                discovery_join();
            } else if (now_ms - s_state_enter_ms >= s_election_ms) {
                s_role = UMESH_ROLE_COORDINATOR;
                s_node_id = UMESH_ADDR_COORDINATOR;
                discovery_auto_promote_to_coordinator();
                mac_set_node_id(UMESH_ADDR_COORDINATOR);
                if (s_routing_mode == UMESH_ROUTING_GRADIENT) {
                    discovery_gradient_set_distance(0);
                    discovery_gradient_send_beacon(0);
                    s_last_gradient_beacon_ms = now_ms;
                }
                s_state = UMESH_STATE_CONNECTED;
                s_state_enter_ms = now_ms;
                s_last_coord_seen_ms = now_ms;
            }
        }
    }

    if (s_state == UMESH_STATE_JOINING &&
        s_role != UMESH_ROLE_COORDINATOR) {
        if (now_ms - s_last_join_ms >= UMESH_JOIN_RETRY_MS) {
            discovery_join();
            s_last_join_ms = now_ms;
        }
    }

    if (s_state == UMESH_STATE_CONNECTED) {
        if (s_routing_mode == UMESH_ROUTING_GRADIENT) {
            if (s_role == UMESH_ROLE_COORDINATOR) {
                discovery_gradient_set_distance(0);
                if ((s_last_gradient_beacon_ms == 0) ||
                    (now_ms - s_last_gradient_beacon_ms >= s_gradient_beacon_ms)) {
                    discovery_gradient_send_beacon(0);
                    s_last_gradient_beacon_ms = now_ms;
                }
            } else if (discovery_gradient_poll_rebroadcast(&rebroadcast_distance)) {
                discovery_gradient_send_beacon(rebroadcast_distance);
            }
        }

        if (now_ms - s_last_route_update_ms >= UMESH_ROUTE_UPDATE_MS) {
            umesh_frame_t frame;
            memset(&frame, 0, sizeof(frame));
            frame.net_id = s_net_id;
            frame.dst = UMESH_ADDR_BROADCAST;
            frame.src = s_node_id;
            frame.cmd = UMESH_CMD_ROUTE_UPDATE;
            frame.flags = UMESH_FLAG_PRIO_NORMAL;
            frame.seq_num = next_seq();
            frame.hop_count = UMESH_MAX_HOP_COUNT;
            mac_send(&frame);
            s_last_route_update_ms = now_ms;
        }
    }

    if (s_role_cfg == UMESH_ROLE_AUTO &&
        s_state == UMESH_STATE_CONNECTED &&
        s_role != UMESH_ROLE_COORDINATOR) {
        if (now_ms - s_last_coord_seen_ms > UMESH_NODE_TIMEOUT_MS) {
            enter_scanning();
        }
    }
}

void net_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    on_mac_rx((umesh_frame_t *)frame, rssi);
}
