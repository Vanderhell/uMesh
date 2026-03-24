#include "discovery.h"
#include "routing.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#include <string.h>
#include <stdlib.h>

/*
 * Discovery protocol:
 *   JOIN / ASSIGN / LEAVE / NODE_{JOINED,LEFT} / ROUTE_UPDATE
 * Plus auto-mesh election:
 *   ELECTION        payload: 6-byte candidate MAC
 *   ELECTION_RESULT payload: 6-byte winner MAC
 */

static uint8_t      s_net_id = 0;
static uint8_t      s_node_id = UMESH_ADDR_UNASSIGNED;
static umesh_role_t s_role = UMESH_ROLE_END_NODE;
static uint16_t     s_seq_num = 0;

static uint8_t      s_assigned_id = UMESH_ADDR_UNASSIGNED;
static bool         s_joined = false;
static uint8_t      s_next_assign_id = 0x02;

static uint32_t     s_scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
static uint32_t     s_election_ms = UMESH_ELECTION_TIMEOUT_MS;
static uint8_t      s_local_mac[6] = {0};
static uint32_t     s_now_ms = 0;

static bool         s_auto_seen_coordinator = false;
static bool         s_auto_saw_lower_mac = false;
static bool         s_auto_seen_result = false;
static uint8_t      s_auto_winner_mac[6] = {0};

static bool         s_gradient_enabled = false;
static uint8_t      s_gradient_distance = UINT8_MAX;
static bool         s_gradient_ready = false;
static bool         s_gradient_rebroadcast_pending = false;
static uint32_t     s_gradient_rebroadcast_at_ms = 0;
static uint32_t     s_gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;

static uint16_t next_seq(void)
{
    s_seq_num = (uint16_t)((s_seq_num + 1u) & 0x0FFF);
    if (s_seq_num == 0) {
        s_seq_num = 1;
    }
    return s_seq_num;
}

static int mac_compare(const uint8_t a[6], const uint8_t b[6])
{
    size_t i;
    for (i = 0; i < 6; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static void derive_fallback_mac(uint8_t net_id, uint8_t node_id, uint8_t out[6])
{
    out[0] = 0xAC;
    out[1] = 0x00;
    out[2] = net_id;
    out[3] = node_id;
    out[4] = 0x5E;
    out[5] = 0x01;
}

static uint8_t derive_auto_node_id_from_mac(const uint8_t mac[6])
{
    uint8_t id = (uint8_t)(((uint16_t)mac[4] << 1) ^ mac[5]);
    id = (uint8_t)(2u + (id % 252u)); /* 0x02..0xFD */
    if (id == UMESH_ADDR_COORDINATOR) id = 0x02;
    return id;
}

static uint32_t gradient_jitter_delay_ms(void)
{
    uint32_t max = s_gradient_jitter_max_ms;
    if (max < 50u) max = 50u;
    if (max <= 50u) return 50u;
    return 50u + (uint32_t)(rand() % (int)(max - 50u + 1u));
}

static umesh_result_t send_frame(uint8_t dst, uint8_t cmd,
                                 const uint8_t *payload, uint8_t plen,
                                 uint8_t flags)
{
    umesh_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.net_id = s_net_id;
    frame.dst = dst;
    frame.src = s_node_id;
    frame.flags = flags;
    frame.cmd = cmd;
    frame.seq_num = next_seq();
    frame.hop_count = UMESH_MAX_HOP_COUNT;
    frame.payload_len = plen;
    if (plen > 0 && payload) {
        memcpy(frame.payload, payload, plen);
    }
    return mac_send(&frame);
}

umesh_result_t discovery_init(uint8_t net_id, uint8_t node_id,
                              umesh_role_t role)
{
    s_net_id = net_id;
    s_node_id = node_id;
    s_role = role;
    s_assigned_id = node_id;
    s_joined = (node_id != UMESH_ADDR_UNASSIGNED) &&
               (node_id != UMESH_ADDR_COORDINATOR ||
                role == UMESH_ROLE_COORDINATOR);
    s_next_assign_id = 0x02;

    if (role == UMESH_ROLE_COORDINATOR) {
        s_node_id = UMESH_ADDR_COORDINATOR;
        s_assigned_id = UMESH_ADDR_COORDINATOR;
        s_joined = true;
    } else if (role == UMESH_ROLE_AUTO) {
        s_role = UMESH_ROLE_ROUTER;
        s_node_id = UMESH_ADDR_UNASSIGNED;
        s_assigned_id = UMESH_ADDR_UNASSIGNED;
        s_joined = false;
    }

    s_auto_seen_coordinator = false;
    s_auto_saw_lower_mac = false;
    s_auto_seen_result = false;
    memset(s_auto_winner_mac, 0, sizeof(s_auto_winner_mac));
    s_now_ms = 0;

    s_gradient_enabled = false;
    s_gradient_distance = UINT8_MAX;
    s_gradient_ready = false;
    s_gradient_rebroadcast_pending = false;
    s_gradient_rebroadcast_at_ms = 0;
    s_gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;

    derive_fallback_mac(net_id, node_id, s_local_mac);

    return UMESH_OK;
}

void discovery_set_auto_timing(uint32_t scan_ms, uint32_t election_ms)
{
    s_scan_ms = (scan_ms == 0) ? UMESH_DISCOVER_TIMEOUT_MS : scan_ms;
    s_election_ms = (election_ms == 0) ? UMESH_ELECTION_TIMEOUT_MS : election_ms;
}

void discovery_set_now(uint32_t now_ms)
{
    s_now_ms = now_ms;
}

void discovery_enable_gradient(bool enabled, uint32_t jitter_max_ms)
{
    s_gradient_enabled = enabled;
    if (jitter_max_ms == 0) {
        s_gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;
    } else {
        s_gradient_jitter_max_ms = jitter_max_ms;
    }
    if (!enabled) {
        s_gradient_distance = UINT8_MAX;
        s_gradient_ready = false;
        s_gradient_rebroadcast_pending = false;
        s_gradient_rebroadcast_at_ms = 0;
    }
}

void discovery_set_local_mac(const uint8_t mac[6])
{
    if (mac) {
        memcpy(s_local_mac, mac, 6);
    }
}

uint32_t discovery_get_scan_ms(void)
{
    return s_scan_ms;
}

uint32_t discovery_get_election_ms(void)
{
    return s_election_ms;
}

void discovery_get_local_mac(uint8_t mac[6])
{
    if (mac) {
        memcpy(mac, s_local_mac, 6);
    }
}

void discovery_set_role(umesh_role_t role)
{
    s_role = role;
    if (role == UMESH_ROLE_COORDINATOR) {
        s_node_id = UMESH_ADDR_COORDINATOR;
        s_assigned_id = UMESH_ADDR_COORDINATOR;
        s_joined = true;
        s_next_assign_id = 0x02;
    } else if (role == UMESH_ROLE_ROUTER) {
        if (s_node_id == UMESH_ADDR_COORDINATOR) {
            s_node_id = UMESH_ADDR_UNASSIGNED;
            s_assigned_id = UMESH_ADDR_UNASSIGNED;
        }
        s_joined = (s_node_id != UMESH_ADDR_UNASSIGNED);
    }
}

umesh_role_t discovery_get_role(void)
{
    return s_role;
}

void discovery_set_node_id(uint8_t node_id)
{
    s_node_id = node_id;
    s_assigned_id = node_id;
    s_joined = (node_id != UMESH_ADDR_UNASSIGNED);
}

umesh_result_t discovery_join(void)
{
    if (s_role == UMESH_ROLE_COORDINATOR) {
        s_joined = true;
        return UMESH_OK;
    }

    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_JOIN,
                      NULL, 0, UMESH_FLAG_PRIO_HIGH);
}

umesh_result_t discovery_start_election(void)
{
    s_auto_saw_lower_mac = false;
    s_auto_seen_result = false;
    memset(s_auto_winner_mac, 0, sizeof(s_auto_winner_mac));

    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ELECTION,
                      s_local_mac, 6, UMESH_FLAG_PRIO_HIGH);
}

umesh_result_t discovery_broadcast_election_result(void)
{
    static const uint8_t zero_mac[6] = {0};
    if (memcmp(s_auto_winner_mac, zero_mac, 6) == 0) {
        memcpy(s_auto_winner_mac, s_local_mac, 6);
    }
    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ELECTION_RESULT,
                      s_auto_winner_mac, 6, UMESH_FLAG_PRIO_HIGH);
}

void discovery_leave(void)
{
    send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_LEAVE,
               NULL, 0, UMESH_FLAG_PRIO_HIGH);
    s_joined = false;
    s_node_id = UMESH_ADDR_UNASSIGNED;
}

bool discovery_is_joined(void)
{
    return s_joined;
}

uint8_t discovery_get_node_id(void)
{
    return s_node_id;
}

bool discovery_auto_seen_coordinator(void)
{
    return s_auto_seen_coordinator;
}

void discovery_auto_clear_scan_flag(void)
{
    s_auto_seen_coordinator = false;
}

void discovery_auto_clear_election_flags(void)
{
    s_auto_saw_lower_mac = false;
    s_auto_seen_result = false;
    memset(s_auto_winner_mac, 0, sizeof(s_auto_winner_mac));
}

bool discovery_auto_saw_lower_mac(void)
{
    return s_auto_saw_lower_mac;
}

bool discovery_auto_seen_election_result(void)
{
    return s_auto_seen_result;
}

void discovery_auto_promote_to_coordinator(void)
{
    s_role = UMESH_ROLE_COORDINATOR;
    s_node_id = UMESH_ADDR_COORDINATOR;
    s_assigned_id = UMESH_ADDR_COORDINATOR;
    s_joined = true;
    s_next_assign_id = 0x02;
    memcpy(s_auto_winner_mac, s_local_mac, 6);
    discovery_broadcast_election_result();
}

void discovery_gradient_reset(void)
{
    s_gradient_distance = UINT8_MAX;
    s_gradient_ready = false;
    s_gradient_rebroadcast_pending = false;
    s_gradient_rebroadcast_at_ms = 0;
}

void discovery_gradient_set_distance(uint8_t distance)
{
    s_gradient_distance = distance;
    s_gradient_ready = (distance != UINT8_MAX);
    s_gradient_rebroadcast_pending = false;
    s_gradient_rebroadcast_at_ms = 0;
}

uint8_t discovery_gradient_distance(void)
{
    return s_gradient_distance;
}

bool discovery_gradient_ready(void)
{
    return s_gradient_ready;
}

bool discovery_gradient_poll_rebroadcast(uint8_t *distance_out)
{
    if (!distance_out) return false;
    if (!s_gradient_rebroadcast_pending) return false;
    if (s_now_ms < s_gradient_rebroadcast_at_ms) return false;
    *distance_out = s_gradient_distance;
    s_gradient_rebroadcast_pending = false;
    return true;
}

umesh_result_t discovery_gradient_send_beacon(uint8_t distance)
{
    uint8_t payload[1];
    payload[0] = distance;
    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_GRADIENT_BEACON,
                      payload, 1, UMESH_FLAG_PRIO_NORMAL);
}

void discovery_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    if (!frame) return;
    if (frame->net_id != s_net_id) return;

    switch (frame->cmd) {
    case UMESH_CMD_JOIN:
        if (s_role != UMESH_ROLE_COORDINATOR) break;
        if (frame->src != UMESH_ADDR_UNASSIGNED) break;
        {
            uint8_t new_id = s_next_assign_id++;
            if (s_next_assign_id > 0xFD) s_next_assign_id = 0x02;

            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ASSIGN,
                       &new_id, 1, UMESH_FLAG_PRIO_HIGH);
            routing_add(new_id, new_id, 1, rssi, s_now_ms);
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_JOINED,
                       &new_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_ASSIGN:
        if (s_role == UMESH_ROLE_COORDINATOR) break;
        if (s_joined) break;
        if (frame->payload_len < 1) break;
        {
            uint8_t new_id = frame->payload[0];
            s_node_id = new_id;
            s_assigned_id = new_id;
            s_joined = true;
            routing_add(UMESH_ADDR_COORDINATOR,
                        UMESH_ADDR_COORDINATOR, 1, rssi, s_now_ms);
        }
        break;

    case UMESH_CMD_LEAVE:
        if (s_role == UMESH_ROLE_COORDINATOR) {
            uint8_t left_id = frame->src;
            routing_remove(left_id);
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_LEFT,
                       &left_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_NODE_JOINED:
        if (frame->payload_len >= 1) {
            routing_add(frame->payload[0],
                        UMESH_ADDR_COORDINATOR, 1, rssi, s_now_ms);
        }
        break;

    case UMESH_CMD_NODE_LEFT:
        if (frame->payload_len >= 1) {
            routing_remove(frame->payload[0]);
        }
        break;

    case UMESH_CMD_ROUTE_UPDATE:
        if (frame->src == UMESH_ADDR_COORDINATOR) {
            s_auto_seen_coordinator = true;
        }
        if (s_role != UMESH_ROLE_END_NODE) {
            routing_add(frame->src, frame->src, 1, rssi, s_now_ms);
        }
        break;

    case UMESH_CMD_GRADIENT_BEACON:
        if (frame->payload_len < 1) break;
        {
            uint8_t recv_distance = frame->payload[0];
            uint8_t candidate;

            neighbor_update(frame->src, recv_distance, rssi, s_now_ms);

            if (!s_gradient_enabled) break;
            if (s_role == UMESH_ROLE_COORDINATOR) {
                s_gradient_distance = 0;
                s_gradient_ready = true;
                s_gradient_rebroadcast_pending = false;
                break;
            }
            if (recv_distance >= UMESH_MAX_HOP_COUNT) break;

            candidate = (uint8_t)(recv_distance + 1u);
            if (candidate < s_gradient_distance) {
                s_gradient_distance = candidate;
                s_gradient_ready = true;

                if (candidate < UMESH_MAX_HOP_COUNT) {
                    s_gradient_rebroadcast_pending = true;
                    s_gradient_rebroadcast_at_ms =
                        s_now_ms + gradient_jitter_delay_ms();
                } else {
                    s_gradient_rebroadcast_pending = false;
                }
            }
        }
        break;

    case UMESH_CMD_GRADIENT_UPDATE:
        if (frame->payload_len >= 2) {
            neighbor_update(frame->payload[0], frame->payload[1],
                            rssi, s_now_ms);
        }
        break;

    case UMESH_CMD_ELECTION:
        if (frame->payload_len < 6) break;
        {
            int cmp = mac_compare(frame->payload, s_local_mac);
            if (cmp < 0) {
                s_auto_saw_lower_mac = true;
                memcpy(s_auto_winner_mac, frame->payload, 6);
            }
        }
        break;

    case UMESH_CMD_ELECTION_RESULT:
        if (frame->payload_len < 6) break;
        s_auto_seen_result = true;
        memcpy(s_auto_winner_mac, frame->payload, 6);
        {
            int cmp = mac_compare(s_auto_winner_mac, s_local_mac);
            if (cmp < 0) {
                /* Lower-MAC winner overrides local role. */
                s_role = UMESH_ROLE_ROUTER;
                if (s_node_id == UMESH_ADDR_COORDINATOR) {
                    s_node_id = UMESH_ADDR_UNASSIGNED;
                    s_assigned_id = UMESH_ADDR_UNASSIGNED;
                }
                if (s_node_id == UMESH_ADDR_UNASSIGNED) {
                    s_node_id = derive_auto_node_id_from_mac(s_local_mac);
                    s_assigned_id = s_node_id;
                }
                s_joined = true;
            } else if (cmp == 0) {
                /* Result confirms we are the elected coordinator. */
                s_role = UMESH_ROLE_COORDINATOR;
                s_node_id = UMESH_ADDR_COORDINATOR;
                s_assigned_id = UMESH_ADDR_COORDINATOR;
                s_joined = true;
                s_next_assign_id = 0x02;
            } else {
                /* Higher-MAC winner is ignored; local node may still win. */
            }
        }
        break;

    default:
        break;
    }
}
