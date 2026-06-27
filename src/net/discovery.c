#include "discovery.h"
#include "routing.h"
#include "../context.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#include <stdlib.h>
#include <string.h>

static uint16_t next_seq(umesh_ctx_t *ctx)
{
    ctx->discovery.seq_num = (uint16_t)((ctx->discovery.seq_num + 1u) & 0x0FFF);
    if (ctx->discovery.seq_num == 0) {
        ctx->discovery.seq_num = 1;
    }
    return ctx->discovery.seq_num;
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
    id = (uint8_t)(2u + (id % 252u));
    if (id == UMESH_ADDR_COORDINATOR) id = 0x02;
    return id;
}

static uint32_t gradient_jitter_delay_ms(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint32_t max = ctx->discovery.gradient_jitter_max_ms;
    if (max < 50u) max = 50u;
    if (max <= 50u) return 50u;
    return 50u + (uint32_t)rand() % (max - 50u + 1u);
}

static umesh_result_t send_frame(uint8_t dst, uint8_t cmd,
                                 const uint8_t *payload, uint8_t plen,
                                 uint8_t flags)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    umesh_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.wire_version = UMESH_WIRE_VERSION;
    frame.net_id = ctx->discovery.net_id;
    frame.dst = dst;
    frame.src = ctx->discovery.node_id;
    frame.link_src = ctx->discovery.node_id;
    frame.link_dst = dst;
    frame.flags = flags;
    frame.cmd = cmd;
    frame.seq_num = next_seq(ctx);
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
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.net_id = net_id;
    ctx->discovery.node_id = node_id;
    ctx->discovery.role = role;
    ctx->discovery.assigned_id = node_id;
    ctx->discovery.joined = (node_id != UMESH_ADDR_UNASSIGNED) &&
               (node_id != UMESH_ADDR_COORDINATOR || role == UMESH_ROLE_COORDINATOR);
    ctx->discovery.next_assign_id = 0x02;
    ctx->discovery.scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
    ctx->discovery.election_ms = UMESH_ELECTION_TIMEOUT_MS;
    ctx->discovery.seq_num = 0;

    if (role == UMESH_ROLE_COORDINATOR) {
        ctx->discovery.node_id = UMESH_ADDR_COORDINATOR;
        ctx->discovery.assigned_id = UMESH_ADDR_COORDINATOR;
        ctx->discovery.joined = true;
    } else if (role == UMESH_ROLE_AUTO) {
        ctx->discovery.role = UMESH_ROLE_ROUTER;
        ctx->discovery.node_id = UMESH_ADDR_UNASSIGNED;
        ctx->discovery.assigned_id = UMESH_ADDR_UNASSIGNED;
        ctx->discovery.joined = false;
    }

    ctx->discovery.auto_seen_coordinator = false;
    ctx->discovery.auto_saw_lower_mac = false;
    ctx->discovery.auto_seen_result = false;
    memset(ctx->discovery.auto_winner_mac, 0, sizeof(ctx->discovery.auto_winner_mac));
    ctx->discovery.now_ms = 0;
    ctx->discovery.gradient_enabled = false;
    ctx->discovery.gradient_distance = UINT8_MAX;
    ctx->discovery.gradient_ready = false;
    ctx->discovery.gradient_rebroadcast_pending = false;
    ctx->discovery.gradient_rebroadcast_at_ms = 0;
    ctx->discovery.gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;

    derive_fallback_mac(net_id, node_id, ctx->discovery.local_mac);
    return UMESH_OK;
}

void discovery_set_auto_timing(uint32_t scan_ms, uint32_t election_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.scan_ms = (scan_ms == 0) ? UMESH_DISCOVER_TIMEOUT_MS : scan_ms;
    ctx->discovery.election_ms = (election_ms == 0) ? UMESH_ELECTION_TIMEOUT_MS : election_ms;
}

void discovery_set_now(uint32_t now_ms)
{
    umesh_current_ctx()->discovery.now_ms = now_ms;
}

void discovery_enable_gradient(bool enabled, uint32_t jitter_max_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.gradient_enabled = enabled;
    ctx->discovery.gradient_jitter_max_ms =
        (jitter_max_ms == 0) ? UMESH_GRADIENT_JITTER_MAX_MS : jitter_max_ms;
    if (!enabled) {
        ctx->discovery.gradient_distance = UINT8_MAX;
        ctx->discovery.gradient_ready = false;
        ctx->discovery.gradient_rebroadcast_pending = false;
        ctx->discovery.gradient_rebroadcast_at_ms = 0;
    }
}

void discovery_set_local_mac(const uint8_t mac[6])
{
    if (mac) {
        memcpy(umesh_current_ctx()->discovery.local_mac, mac, 6);
    }
}

uint32_t discovery_get_scan_ms(void)
{
    return umesh_current_ctx()->discovery.scan_ms;
}

uint32_t discovery_get_election_ms(void)
{
    return umesh_current_ctx()->discovery.election_ms;
}

void discovery_get_local_mac(uint8_t mac[6])
{
    if (mac) {
        memcpy(mac, umesh_current_ctx()->discovery.local_mac, 6);
    }
}

void discovery_set_role(umesh_role_t role)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.role = role;
    if (role == UMESH_ROLE_COORDINATOR) {
        ctx->discovery.node_id = UMESH_ADDR_COORDINATOR;
        ctx->discovery.assigned_id = UMESH_ADDR_COORDINATOR;
        ctx->discovery.joined = true;
        ctx->discovery.next_assign_id = 0x02;
    } else if (role == UMESH_ROLE_ROUTER) {
        if (ctx->discovery.node_id == UMESH_ADDR_COORDINATOR) {
            ctx->discovery.node_id = UMESH_ADDR_UNASSIGNED;
            ctx->discovery.assigned_id = UMESH_ADDR_UNASSIGNED;
        }
        ctx->discovery.joined = (ctx->discovery.node_id != UMESH_ADDR_UNASSIGNED);
    }
}

umesh_role_t discovery_get_role(void)
{
    return umesh_current_ctx()->discovery.role;
}

void discovery_set_node_id(uint8_t node_id)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.node_id = node_id;
    ctx->discovery.assigned_id = node_id;
    ctx->discovery.joined = (node_id != UMESH_ADDR_UNASSIGNED);
}

umesh_result_t discovery_join(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) {
        ctx->discovery.joined = true;
        return UMESH_OK;
    }
    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_JOIN, NULL, 0, UMESH_FLAG_PRIO_HIGH);
}

umesh_result_t discovery_start_election(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.auto_saw_lower_mac = false;
    ctx->discovery.auto_seen_result = false;
    memset(ctx->discovery.auto_winner_mac, 0, sizeof(ctx->discovery.auto_winner_mac));
    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ELECTION,
                      ctx->discovery.local_mac, 6, UMESH_FLAG_PRIO_HIGH);
}

umesh_result_t discovery_broadcast_election_result(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    static const uint8_t zero_mac[6] = {0};
    if (memcmp(ctx->discovery.auto_winner_mac, zero_mac, 6) == 0) {
        memcpy(ctx->discovery.auto_winner_mac, ctx->discovery.local_mac, 6);
    }
    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ELECTION_RESULT,
                      ctx->discovery.auto_winner_mac, 6, UMESH_FLAG_PRIO_HIGH);
}

void discovery_leave(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_LEAVE, NULL, 0, UMESH_FLAG_PRIO_HIGH);
    ctx->discovery.joined = false;
    ctx->discovery.node_id = UMESH_ADDR_UNASSIGNED;
}

bool discovery_is_joined(void)
{
    return umesh_current_ctx()->discovery.joined;
}

uint8_t discovery_get_node_id(void)
{
    return umesh_current_ctx()->discovery.node_id;
}

bool discovery_auto_seen_coordinator(void)
{
    return umesh_current_ctx()->discovery.auto_seen_coordinator;
}

void discovery_auto_clear_scan_flag(void)
{
    umesh_current_ctx()->discovery.auto_seen_coordinator = false;
}

void discovery_auto_clear_election_flags(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.auto_saw_lower_mac = false;
    ctx->discovery.auto_seen_result = false;
    memset(ctx->discovery.auto_winner_mac, 0, sizeof(ctx->discovery.auto_winner_mac));
}

bool discovery_auto_saw_lower_mac(void)
{
    return umesh_current_ctx()->discovery.auto_saw_lower_mac;
}

bool discovery_auto_seen_election_result(void)
{
    return umesh_current_ctx()->discovery.auto_seen_result;
}

void discovery_auto_promote_to_coordinator(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.role = UMESH_ROLE_COORDINATOR;
    ctx->discovery.node_id = UMESH_ADDR_COORDINATOR;
    ctx->discovery.assigned_id = UMESH_ADDR_COORDINATOR;
    ctx->discovery.joined = true;
    ctx->discovery.next_assign_id = 0x02;
    memcpy(ctx->discovery.auto_winner_mac, ctx->discovery.local_mac, 6);
    discovery_broadcast_election_result();
}

void discovery_gradient_reset(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.gradient_distance = UINT8_MAX;
    ctx->discovery.gradient_ready = false;
    ctx->discovery.gradient_rebroadcast_pending = false;
    ctx->discovery.gradient_rebroadcast_at_ms = 0;
}

void discovery_gradient_set_distance(uint8_t distance)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.gradient_distance = distance;
    ctx->discovery.gradient_ready = (distance != UINT8_MAX);
    ctx->discovery.gradient_rebroadcast_pending = false;
    ctx->discovery.gradient_rebroadcast_at_ms = 0;
}

uint8_t discovery_gradient_distance(void)
{
    return umesh_current_ctx()->discovery.gradient_distance;
}

bool discovery_gradient_ready(void)
{
    return umesh_current_ctx()->discovery.gradient_ready;
}

bool discovery_gradient_poll_rebroadcast(uint8_t *distance_out)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!distance_out) return false;
    if (!ctx->discovery.gradient_rebroadcast_pending) return false;
    if (ctx->discovery.now_ms < ctx->discovery.gradient_rebroadcast_at_ms) return false;
    *distance_out = ctx->discovery.gradient_distance;
    ctx->discovery.gradient_rebroadcast_pending = false;
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
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!frame) return;
    if (frame->net_id != ctx->discovery.net_id) return;

    switch (frame->cmd) {
    case UMESH_CMD_JOIN:
        if (ctx->discovery.role != UMESH_ROLE_COORDINATOR) break;
        if (frame->src != UMESH_ADDR_UNASSIGNED) break;
        {
            uint8_t new_id = ctx->discovery.next_assign_id++;
            if (ctx->discovery.next_assign_id > 0xFD) ctx->discovery.next_assign_id = 0x02;

            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ASSIGN,
                       &new_id, 1, UMESH_FLAG_PRIO_HIGH);
            routing_add(new_id, new_id, 1, rssi, ctx->discovery.now_ms);
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_JOINED,
                       &new_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_ASSIGN:
        if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) break;
        if (ctx->discovery.joined) break;
        if (frame->payload_len < 1) break;
        {
            uint8_t new_id = frame->payload[0];
            ctx->discovery.node_id = new_id;
            ctx->discovery.assigned_id = new_id;
            ctx->discovery.joined = true;
            routing_add(UMESH_ADDR_COORDINATOR,
                        UMESH_ADDR_COORDINATOR, 1, rssi, ctx->discovery.now_ms);
        }
        break;

    case UMESH_CMD_LEAVE:
        if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) {
            uint8_t left_id = frame->src;
            routing_remove(left_id);
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_LEFT,
                       &left_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_NODE_JOINED:
        if (frame->payload_len >= 1) {
            routing_add(frame->payload[0],
                        UMESH_ADDR_COORDINATOR, 1, rssi, ctx->discovery.now_ms);
        }
        break;

    case UMESH_CMD_NODE_LEFT:
        if (frame->payload_len >= 1) {
            routing_remove(frame->payload[0]);
        }
        break;

    case UMESH_CMD_ROUTE_UPDATE:
        if (frame->src == UMESH_ADDR_COORDINATOR) {
            ctx->discovery.auto_seen_coordinator = true;
        }
        if (ctx->discovery.role != UMESH_ROLE_END_NODE) {
            routing_add(frame->src, frame->src, 1, rssi, ctx->discovery.now_ms);
        }
        break;

    case UMESH_CMD_GRADIENT_BEACON:
        if (frame->payload_len < 1) break;
        {
            uint8_t recv_distance = frame->payload[0];
            uint8_t candidate;

            neighbor_update(frame->src, recv_distance, rssi, ctx->discovery.now_ms);

            if (!ctx->discovery.gradient_enabled) break;
            if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) {
                ctx->discovery.gradient_distance = 0;
                ctx->discovery.gradient_ready = true;
                ctx->discovery.gradient_rebroadcast_pending = false;
                break;
            }
            if (recv_distance >= UMESH_MAX_HOP_COUNT) break;

            candidate = (uint8_t)(recv_distance + 1u);
            if (candidate < ctx->discovery.gradient_distance) {
                ctx->discovery.gradient_distance = candidate;
                ctx->discovery.gradient_ready = true;

                if (candidate < UMESH_MAX_HOP_COUNT) {
                    ctx->discovery.gradient_rebroadcast_pending = true;
                    ctx->discovery.gradient_rebroadcast_at_ms =
                        ctx->discovery.now_ms + gradient_jitter_delay_ms();
                } else {
                    ctx->discovery.gradient_rebroadcast_pending = false;
                }
            }
        }
        break;

    case UMESH_CMD_GRADIENT_UPDATE:
        if (frame->payload_len >= 2) {
            neighbor_update(frame->payload[0], frame->payload[1],
                            rssi, ctx->discovery.now_ms);
        }
        break;

    case UMESH_CMD_ELECTION:
        if (frame->payload_len < 6) break;
        if (mac_compare(frame->payload, ctx->discovery.local_mac) < 0) {
            ctx->discovery.auto_saw_lower_mac = true;
            memcpy(ctx->discovery.auto_winner_mac, frame->payload, 6);
        }
        break;

    case UMESH_CMD_ELECTION_RESULT:
        if (frame->payload_len < 6) break;
        ctx->discovery.auto_seen_result = true;
        memcpy(ctx->discovery.auto_winner_mac, frame->payload, 6);
        {
            int cmp = mac_compare(ctx->discovery.auto_winner_mac, ctx->discovery.local_mac);
            if (cmp < 0) {
                ctx->discovery.role = UMESH_ROLE_ROUTER;
                if (ctx->discovery.node_id == UMESH_ADDR_COORDINATOR) {
                    ctx->discovery.node_id = UMESH_ADDR_UNASSIGNED;
                    ctx->discovery.assigned_id = UMESH_ADDR_UNASSIGNED;
                }
                if (ctx->discovery.node_id == UMESH_ADDR_UNASSIGNED) {
                    ctx->discovery.node_id = derive_auto_node_id_from_mac(ctx->discovery.local_mac);
                    ctx->discovery.assigned_id = ctx->discovery.node_id;
                }
                ctx->discovery.joined = true;
            } else if (cmp == 0) {
                ctx->discovery.role = UMESH_ROLE_COORDINATOR;
                ctx->discovery.node_id = UMESH_ADDR_COORDINATOR;
                ctx->discovery.assigned_id = UMESH_ADDR_COORDINATOR;
                ctx->discovery.joined = true;
                ctx->discovery.next_assign_id = 0x02;
            }
        }
        break;

    default:
        break;
    }
}
