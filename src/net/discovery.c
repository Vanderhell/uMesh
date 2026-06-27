#include "discovery.h"
#include "routing.h"
#include "../context.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#if UMESH_ENABLE_POWER_MANAGEMENT
#include "../power/power.h"
#endif
#include <stdlib.h>
#include <string.h>

#define JOIN_REQ_PAYLOAD_SIZE  15u
#define JOIN_ACK_PAYLOAD_SIZE  26u
#define ELECTION_PAYLOAD_SIZE  10u

static bool mac_is_zero(const uint8_t mac[6])
{
    static const uint8_t zero[6] = {0};
    return memcmp(mac, zero, 6) == 0;
}

static bool mac_is_valid_identity(const uint8_t mac[6])
{
    if (!mac) return false;
    if (mac_is_zero(mac)) return false;
    if (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF &&
        mac[3] == 0xFF && mac[4] == 0xFF && mac[5] == 0xFF) {
        return false;
    }
    return true;
}

static void write_u32le(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
    out[2] = (uint8_t)((value >> 16) & 0xFF);
    out[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t read_u32le(const uint8_t in[4])
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static umesh_join_tx_t *join_cache_find(umesh_ctx_t *ctx,
                                        const uint8_t requester_mac[6],
                                        uint32_t token)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        umesh_join_tx_t *entry = &ctx->discovery.join_cache[i];
        if (!entry->valid) continue;
        if (entry->token != token) continue;
        if (memcmp(entry->requester_mac, requester_mac, 6) == 0) {
            return entry;
        }
    }
    return NULL;
}

static umesh_join_tx_t *join_cache_alloc(umesh_ctx_t *ctx)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        if (!ctx->discovery.join_cache[i].valid) {
            return &ctx->discovery.join_cache[i];
        }
    }
    return NULL;
}

static umesh_active_node_t *active_node_find(umesh_ctx_t *ctx, uint8_t node_id)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        if (ctx->discovery.active_nodes[i].valid &&
            ctx->discovery.active_nodes[i].node_id == node_id) {
            return &ctx->discovery.active_nodes[i];
        }
    }
    return NULL;
}

static bool active_node_in_use(umesh_ctx_t *ctx, uint8_t node_id)
{
    return active_node_find(ctx, node_id) != NULL;
}

static umesh_result_t active_node_add(umesh_ctx_t *ctx, uint8_t node_id,
                                      const uint8_t node_mac[6],
                                      uint32_t now_ms)
{
    uint8_t i;
    umesh_active_node_t *slot = NULL;

    if (node_id == UMESH_ADDR_BROADCAST ||
        node_id == UMESH_ADDR_UNASSIGNED ||
        node_id == UMESH_ADDR_COORDINATOR) {
        return UMESH_ERR_INVALID_DST;
    }
    if (!mac_is_valid_identity(node_mac)) {
        return UMESH_ERR_INVALID_DST;
    }

    if (active_node_find(ctx, node_id)) {
        return UMESH_ERR_INVALID_DST;
    }

    for (i = 0; i < UMESH_MAX_NODES; i++) {
        if (!ctx->discovery.active_nodes[i].valid) {
            slot = &ctx->discovery.active_nodes[i];
            break;
        }
    }
    if (!slot) {
        return UMESH_ERR_NOT_ROUTABLE;
    }

    slot->valid = true;
    slot->node_id = node_id;
    memcpy(slot->node_mac, node_mac, 6);
    slot->last_seen_ms = now_ms;
    return UMESH_OK;
}

static void active_node_remove(umesh_ctx_t *ctx, uint8_t node_id)
{
    umesh_active_node_t *entry = active_node_find(ctx, node_id);
    if (entry) {
        entry->valid = false;
    }
}

static umesh_result_t allocate_node_id(umesh_ctx_t *ctx, uint8_t *node_id_out)
{
    uint8_t start;
    uint8_t pass;
    uint8_t candidate;

    if (!ctx || !node_id_out) return UMESH_ERR_NULL_PTR;
    start = (ctx->discovery.next_assign_id < 0x02u) ? 0x02u : ctx->discovery.next_assign_id;
    for (pass = 0; pass < 2; pass++) {
        for (candidate = (pass == 0) ? start : 0x02u; candidate <= 0xFDu; candidate++) {
            if (!active_node_in_use(ctx, candidate)) {
                *node_id_out = candidate;
                ctx->discovery.next_assign_id = (uint8_t)(candidate + 1u);
                if (ctx->discovery.next_assign_id > 0xFDu) {
                    ctx->discovery.next_assign_id = 0x02u;
                }
                return UMESH_OK;
            }
        }
    }
    return UMESH_ERR_NOT_ROUTABLE;
}

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
    uint8_t i;
    ctx->discovery.net_id = net_id;
    ctx->discovery.node_id = node_id;
    ctx->discovery.role = role;
    ctx->discovery.assigned_id = node_id;
    ctx->discovery.join_token = 0;
    ctx->discovery.election_term = 0;
    ctx->discovery.seen_election_term = 0;
    ctx->discovery.last_join_result = UMESH_OK;
    ctx->discovery.joined = (node_id != UMESH_ADDR_UNASSIGNED) &&
               (node_id != UMESH_ADDR_COORDINATOR || role == UMESH_ROLE_COORDINATOR);
    ctx->discovery.next_assign_id = 0x02;
    ctx->discovery.scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
    ctx->discovery.election_ms = UMESH_ELECTION_TIMEOUT_MS;
    ctx->discovery.seq_num = 0;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        ctx->discovery.join_cache[i].valid = false;
        ctx->discovery.active_nodes[i].valid = false;
    }

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
        ctx->discovery.election_term = 0;
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
    if (node_id != UMESH_ADDR_UNASSIGNED && node_id != UMESH_ADDR_COORDINATOR) {
        ctx->discovery.last_join_result = UMESH_OK;
    }
}

umesh_result_t discovery_join(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) {
        ctx->discovery.joined = true;
        return UMESH_OK;
    }

    if (!mac_is_valid_identity(ctx->discovery.local_mac)) {
        return UMESH_ERR_NOT_INIT;
    }
    if (ctx->discovery.join_token == 0) {
        ctx->discovery.join_token = next_seq(ctx);
    }
    {
        uint8_t payload[JOIN_REQ_PAYLOAD_SIZE];

        memcpy(&payload[0], ctx->discovery.local_mac, 6);
        write_u32le(&payload[6], ctx->discovery.join_token);
        write_u32le(&payload[10], ctx->cfg.security_epoch);
        payload[14] = (uint8_t)ctx->cfg.security;
        ctx->discovery.last_join_result = UMESH_OK;
        return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_JOIN,
                          payload, (uint8_t)sizeof(payload), UMESH_FLAG_PRIO_HIGH);
    }
}

umesh_result_t discovery_start_election(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.auto_saw_lower_mac = false;
    ctx->discovery.auto_seen_result = false;
    ctx->discovery.election_term = (uint32_t)(ctx->discovery.election_term + 1u);
    ctx->discovery.seen_election_term = ctx->discovery.election_term;
    memset(ctx->discovery.auto_winner_mac, 0, sizeof(ctx->discovery.auto_winner_mac));
    {
        uint8_t payload[ELECTION_PAYLOAD_SIZE];

        write_u32le(&payload[0], ctx->discovery.election_term);
        memcpy(&payload[4], ctx->discovery.local_mac, 6);
        return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ELECTION,
                          payload, (uint8_t)sizeof(payload), UMESH_FLAG_PRIO_HIGH);
    }
}

umesh_result_t discovery_broadcast_election_result(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (mac_is_zero(ctx->discovery.auto_winner_mac)) {
        memcpy(ctx->discovery.auto_winner_mac, ctx->discovery.local_mac, 6);
    }
    {
        uint8_t payload[ELECTION_PAYLOAD_SIZE];

        write_u32le(&payload[0], ctx->discovery.election_term);
        memcpy(&payload[4], ctx->discovery.auto_winner_mac, 6);
        return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ELECTION_RESULT,
                          payload, (uint8_t)sizeof(payload), UMESH_FLAG_PRIO_HIGH);
    }
}

void discovery_leave(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_LEAVE, NULL, 0, UMESH_FLAG_PRIO_HIGH);
    ctx->discovery.joined = false;
    ctx->discovery.node_id = UMESH_ADDR_UNASSIGNED;
    ctx->discovery.join_token = 0;
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

void discovery_tick(uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;

    ctx->discovery.now_ms = now_ms;
    for (i = 0; i < UMESH_MAX_NODES; i++) {
        umesh_active_node_t *entry = &ctx->discovery.active_nodes[i];
        if (!entry->valid) continue;
        if (now_ms >= entry->last_seen_ms &&
            now_ms - entry->last_seen_ms > UMESH_NODE_TIMEOUT_MS) {
            uint8_t node_id = entry->node_id;
            entry->valid = false;
            routing_remove(node_id);
            if (ctx->cfg.on_node_left) {
                ctx->cfg.on_node_left(node_id);
            }
        }
    }
}

void discovery_auto_promote_to_coordinator(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->discovery.role = UMESH_ROLE_COORDINATOR;
    ctx->discovery.node_id = UMESH_ADDR_COORDINATOR;
    ctx->discovery.assigned_id = UMESH_ADDR_COORDINATOR;
    ctx->discovery.joined = true;
    ctx->discovery.next_assign_id = 0x02;
    ctx->discovery.seen_election_term = ctx->discovery.election_term;
    memcpy(ctx->discovery.auto_winner_mac, ctx->discovery.local_mac, 6);
    discovery_broadcast_election_result();
}

uint32_t discovery_get_join_token(void)
{
    return umesh_current_ctx()->discovery.join_token;
}

uint32_t discovery_get_election_term(void)
{
    return umesh_current_ctx()->discovery.election_term;
}

umesh_result_t discovery_get_last_join_result(void)
{
    return umesh_current_ctx()->discovery.last_join_result;
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
        if (frame->payload_len < JOIN_REQ_PAYLOAD_SIZE) break;
        {
            uint8_t requester_mac[6];
            uint32_t token;
            uint32_t req_epoch;
            uint8_t req_security;
            umesh_join_tx_t *join_tx;
            uint8_t new_id = 0;
            umesh_result_t ar;
            uint8_t payload[JOIN_ACK_PAYLOAD_SIZE];

            memcpy(requester_mac, &frame->payload[0], 6);
            token = read_u32le(&frame->payload[6]);
            req_epoch = read_u32le(&frame->payload[10]);
            req_security = frame->payload[14];

            if (!mac_is_valid_identity(requester_mac) || token == 0) break;
            if (req_epoch != ctx->cfg.security_epoch) break;
            if (req_security != (uint8_t)ctx->cfg.security) break;

            join_tx = join_cache_find(ctx, requester_mac, token);
            if (join_tx) {
                new_id = join_tx->assigned_id;
                join_tx->last_seen_ms = ctx->discovery.now_ms;
            } else {
                ar = allocate_node_id(ctx, &new_id);
                if (ar != UMESH_OK) {
                    ctx->discovery.last_join_result = ar;
                    break;
                }
                join_tx = join_cache_alloc(ctx);
                if (!join_tx) {
                    ctx->discovery.last_join_result = UMESH_ERR_NOT_ROUTABLE;
                    break;
                }
                memcpy(join_tx->requester_mac, requester_mac, 6);
                join_tx->token = token;
                join_tx->assigned_id = new_id;
                join_tx->coordinator_term = ctx->discovery.election_term;
                join_tx->security_epoch = ctx->cfg.security_epoch;
                memcpy(join_tx->coordinator_mac, ctx->discovery.local_mac, 6);
                join_tx->last_seen_ms = ctx->discovery.now_ms;
                join_tx->valid = true;

                ar = active_node_add(ctx, new_id, requester_mac, ctx->discovery.now_ms);
                if (ar != UMESH_OK) {
                    join_tx->valid = false;
                    ctx->discovery.last_join_result = ar;
                    break;
                }
                routing_add(new_id, new_id, 1, rssi, ctx->discovery.now_ms);
                if (ctx->cfg.on_node_joined) {
                    ctx->cfg.on_node_joined(new_id);
                }
            }

            memcpy(&payload[0], requester_mac, 6);
            write_u32le(&payload[6], token);
            payload[10] = new_id;
            memcpy(&payload[11], ctx->discovery.local_mac, 6);
            write_u32le(&payload[17], ctx->discovery.election_term);
            write_u32le(&payload[21], ctx->cfg.security_epoch);
            payload[25] = (uint8_t)ctx->cfg.security;
            ctx->discovery.last_join_result = UMESH_OK;
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ASSIGN,
                       payload, (uint8_t)sizeof(payload), UMESH_FLAG_PRIO_HIGH);
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_JOINED,
                       &new_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_ASSIGN:
        if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) break;
        if (ctx->discovery.joined) break;
        if (frame->payload_len < JOIN_ACK_PAYLOAD_SIZE) break;
        {
            uint8_t requester_mac[6];
            uint32_t token;
            uint8_t new_id;
            uint8_t coordinator_mac[6];
            uint32_t coordinator_term;
            uint32_t sec_epoch;
            uint8_t sec_level;

            memcpy(requester_mac, &frame->payload[0], 6);
            token = read_u32le(&frame->payload[6]);
            new_id = frame->payload[10];
            memcpy(coordinator_mac, &frame->payload[11], 6);
            coordinator_term = read_u32le(&frame->payload[17]);
            sec_epoch = read_u32le(&frame->payload[21]);
            sec_level = frame->payload[25];

            if (!mac_is_valid_identity(requester_mac) ||
                memcmp(requester_mac, ctx->discovery.local_mac, 6) != 0) break;
            if (token == 0 || token != ctx->discovery.join_token) break;
            if (sec_epoch != ctx->cfg.security_epoch) break;
            if (sec_level != (uint8_t)ctx->cfg.security) break;
            if (!mac_is_valid_identity(coordinator_mac)) break;
            if (new_id == UMESH_ADDR_BROADCAST ||
                new_id == UMESH_ADDR_UNASSIGNED ||
                new_id == UMESH_ADDR_COORDINATOR) break;
            if (active_node_in_use(ctx, new_id)) break;
            if (coordinator_term < ctx->discovery.seen_election_term) break;

            ctx->discovery.node_id = new_id;
            ctx->discovery.assigned_id = new_id;
            ctx->discovery.joined = true;
            ctx->discovery.join_token = 0;
            ctx->discovery.last_join_result = UMESH_OK;
            memcpy(ctx->discovery.local_mac, requester_mac, 6);
            routing_add(UMESH_ADDR_COORDINATOR,
                        UMESH_ADDR_COORDINATOR, 1, rssi, ctx->discovery.now_ms);
        }
        break;

    case UMESH_CMD_LEAVE:
        if (ctx->discovery.role == UMESH_ROLE_COORDINATOR) {
            uint8_t left_id = frame->src;
            routing_remove(left_id);
            active_node_remove(ctx, left_id);
            if (ctx->cfg.on_node_left) {
                ctx->cfg.on_node_left(left_id);
            }
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

    case UMESH_CMD_POWER_BEACON:
        if (frame->payload_len < 8) break;
        {
            uint32_t light_interval = read_u32le(&frame->payload[0]);
            uint32_t light_window = read_u32le(&frame->payload[4]);

            if (light_interval == 0) break;
            if (light_window > light_interval) break;

#if UMESH_ENABLE_POWER_MANAGEMENT
            ctx->net.light_sleep_interval_ms = light_interval;
            ctx->net.light_listen_window_ms = light_window;
            power_set_light_profile(light_interval, light_window);
#endif
        }
        break;

    case UMESH_CMD_ELECTION:
        if (frame->payload_len < ELECTION_PAYLOAD_SIZE) break;
        {
            uint32_t term = read_u32le(&frame->payload[0]);
            const uint8_t *candidate = &frame->payload[4];

            if (!mac_is_valid_identity(candidate)) break;
            if (term < ctx->discovery.seen_election_term) break;
            if (term > ctx->discovery.seen_election_term) {
                ctx->discovery.seen_election_term = term;
                ctx->discovery.auto_seen_result = false;
            }
            if (mac_compare(candidate, ctx->discovery.local_mac) < 0) {
                ctx->discovery.auto_saw_lower_mac = true;
                memcpy(ctx->discovery.auto_winner_mac, candidate, 6);
            }
        }
        break;

    case UMESH_CMD_ELECTION_RESULT:
        if (frame->payload_len < ELECTION_PAYLOAD_SIZE) break;
        {
            uint32_t term = read_u32le(&frame->payload[0]);
            const uint8_t *winner = &frame->payload[4];
            int cmp;

            if (!mac_is_valid_identity(winner)) break;
            if (term < ctx->discovery.seen_election_term) break;
            if (term > ctx->discovery.seen_election_term) {
                ctx->discovery.seen_election_term = term;
                ctx->discovery.auto_seen_result = false;
            }
            ctx->discovery.auto_seen_result = true;
            memcpy(ctx->discovery.auto_winner_mac, winner, 6);
            cmp = mac_compare(ctx->discovery.auto_winner_mac, ctx->discovery.local_mac);
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
