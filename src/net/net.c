#include "net.h"
#include "discovery.h"
#include "routing.h"
#include "../context.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#include "../sec/sec.h"
#include <string.h>

#define UMESH_JOIN_RETRY_MS 1000u
#define UMESH_ELECTION_RESULT_ANNOUNCE_MS 1000u

static uint16_t next_seq(umesh_ctx_t *ctx)
{
    ctx->net.seq_num = (uint16_t)((ctx->net.seq_num + 1u) & 0x0FFF);
    if (ctx->net.seq_num == 0) {
        sec_regenerate_salt();
        ctx->net.seq_num = 1;
    }
    return ctx->net.seq_num;
}

static void net_dup_cache_reset(umesh_ctx_t *ctx)
{
    uint8_t i;

    for (i = 0; i < UMESH_DUP_CACHE_SIZE; i++) {
        ctx->net.dup_cache[i].valid = false;
    }
    ctx->net.dup_cache_next = 0;
    ctx->net.last_route_result = UMESH_OK;
}

static bool net_dup_cache_match(const umesh_dup_entry_t *entry,
                                const umesh_frame_t *frame)
{
    return entry->valid &&
           entry->src == frame->src &&
           entry->dst == frame->dst &&
           entry->cmd == frame->cmd &&
           entry->flags == frame->flags &&
           entry->seq_num == frame->seq_num;
}

static bool net_dup_cache_seen(umesh_ctx_t *ctx, const umesh_frame_t *frame)
{
    uint8_t i;
    uint8_t insert = ctx->net.dup_cache_next;
    uint32_t now_ms = ctx->net.now_ms;

    for (i = 0; i < UMESH_DUP_CACHE_SIZE; i++) {
        umesh_dup_entry_t *entry = &ctx->net.dup_cache[i];
        if (!entry->valid) {
            continue;
        }
        if (now_ms >= entry->last_seen_ms &&
            now_ms - entry->last_seen_ms > UMESH_DUP_CACHE_TTL_MS) {
            entry->valid = false;
            continue;
        }
        if (net_dup_cache_match(entry, frame)) {
            entry->last_seen_ms = now_ms;
            return true;
        }
    }

    for (i = 0; i < UMESH_DUP_CACHE_SIZE; i++) {
        if (!ctx->net.dup_cache[i].valid) {
            insert = i;
            break;
        }
    }

    ctx->net.dup_cache[insert].src = frame->src;
    ctx->net.dup_cache[insert].dst = frame->dst;
    ctx->net.dup_cache[insert].cmd = frame->cmd;
    ctx->net.dup_cache[insert].flags = frame->flags;
    ctx->net.dup_cache[insert].seq_num = frame->seq_num;
    ctx->net.dup_cache[insert].last_seen_ms = now_ms;
    ctx->net.dup_cache[insert].valid = true;
    ctx->net.dup_cache_next = (uint8_t)((insert + 1u) % UMESH_DUP_CACHE_SIZE);
    return false;
}

static umesh_result_t net_select_next_hop(umesh_ctx_t *ctx,
                                          const umesh_frame_t *frame,
                                          uint8_t *next_hop_out)
{
    umesh_route_entry_t route;

    if (!ctx || !frame || !next_hop_out) {
        return UMESH_ERR_NULL_PTR;
    }
    if (frame->dst == UMESH_ADDR_BROADCAST) {
        *next_hop_out = UMESH_ADDR_BROADCAST;
        return UMESH_OK;
    }

    routing_expire(ctx->net.now_ms);

    if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT &&
        frame->dst == UMESH_ADDR_COORDINATOR) {
        uint8_t my_distance = discovery_gradient_distance();
        uint8_t next_hop = neighbor_find_uphill(my_distance);

        if (next_hop == UMESH_ADDR_BROADCAST) {
            return UMESH_ERR_NOT_ROUTABLE;
        }
        *next_hop_out = next_hop;
        return UMESH_OK;
    }

    if (!routing_find(frame->dst, &route)) {
        return UMESH_ERR_NOT_ROUTABLE;
    }
    if (route.next_hop == UMESH_ADDR_BROADCAST) {
        return UMESH_ERR_NOT_ROUTABLE;
    }

    *next_hop_out = route.next_hop;
    return UMESH_OK;
}

static umesh_result_t net_send_via_route(umesh_ctx_t *ctx, umesh_frame_t *frame)
{
    uint8_t next_hop = UMESH_ADDR_BROADCAST;
    umesh_result_t r;

    r = net_select_next_hop(ctx, frame, &next_hop);
    if (r != UMESH_OK) {
        ctx->net.last_route_result = r;
        return r;
    }

    frame->link_src = ctx->net.node_id;
    frame->link_dst = next_hop;
    r = mac_send(frame);
    ctx->net.last_route_result = r;
    return r;
}

static umesh_result_t net_forward_frame(umesh_ctx_t *ctx, umesh_frame_t *frame)
{
    umesh_result_t r;

    if (!frame) {
        return UMESH_ERR_NULL_PTR;
    }
    if (frame->hop_count == 0) {
        ctx->net.last_route_result = UMESH_ERR_TOO_LONG;
        return UMESH_ERR_TOO_LONG;
    }

    frame->hop_count--;
    if (frame->hop_count == 0) {
        ctx->net.last_route_result = UMESH_ERR_TOO_LONG;
        return UMESH_ERR_TOO_LONG;
    }

    r = net_send_via_route(ctx, frame);
    if (r != UMESH_OK) {
        ctx->net.last_route_result = r;
    }
    return r;
}

static void enter_scanning(umesh_ctx_t *ctx)
{
    ctx->net.role = UMESH_ROLE_ROUTER;
    discovery_set_role(UMESH_ROLE_ROUTER);
    discovery_set_node_id(UMESH_ADDR_UNASSIGNED);
    ctx->net.node_id = UMESH_ADDR_UNASSIGNED;
    mac_set_node_id(UMESH_ADDR_UNASSIGNED);
    discovery_auto_clear_scan_flag();
    discovery_auto_clear_election_flags();
    discovery_gradient_reset();
    ctx->net.state = UMESH_STATE_SCANNING;
    ctx->net.state_enter_ms = ctx->net.now_ms;
    ctx->net.last_coord_seen_ms = ctx->net.now_ms;
}

static void on_mac_rx(umesh_frame_t *frame, int8_t rssi)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!frame) return;
    if (frame->net_id != ctx->net.net_id) return;

    if (frame->src == UMESH_ADDR_COORDINATOR) {
        ctx->net.last_coord_seen_ms = ctx->net.now_ms;
    }

    discovery_set_now(ctx->net.now_ms);
    discovery_on_frame(frame, rssi);

    if (ctx->net.state == UMESH_STATE_JOINING) {
        uint8_t assigned = discovery_get_node_id();
        if (assigned != UMESH_ADDR_UNASSIGNED) {
            ctx->net.node_id = assigned;
            ctx->net.state = UMESH_STATE_CONNECTED;
            ctx->net.role = discovery_get_role();
            mac_set_node_id(assigned);
            ctx->net.last_coord_seen_ms = ctx->net.now_ms;
        }
    }

    if (frame->link_dst != ctx->net.node_id &&
        frame->link_dst != UMESH_ADDR_BROADCAST) {
        return;
    }

    if (frame->flags & UMESH_FLAG_IS_ACK) {
        if (ctx->net.rx_cb) {
            ctx->net.rx_cb(frame, rssi);
        }
        return;
    }

    if (net_dup_cache_seen(ctx, frame)) {
        return;
    }

    if (frame->link_dst == ctx->net.node_id &&
        frame->dst != ctx->net.node_id &&
        frame->dst != UMESH_ADDR_BROADCAST) {
        cca_set_rx_in_progress(false);
        if (net_forward_frame(ctx, frame) != UMESH_OK) {
            ctx->mac.stats.drop_count++;
        }
        return;
    }

    if (ctx->net.rx_cb) {
        ctx->net.rx_cb(frame, rssi);
    }
}

umesh_result_t net_init(uint8_t net_id, uint8_t node_id, umesh_role_t role)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->net.net_id = net_id;
    ctx->net.node_id = node_id;
    ctx->net.role_cfg = role;
    ctx->net.role = (role == UMESH_ROLE_AUTO) ? UMESH_ROLE_ROUTER : role;
    ctx->net.state = UMESH_STATE_SCANNING;
    ctx->net.seq_num = 0;
    ctx->net.last_route_update_ms = 0;
    ctx->net.last_coord_seen_ms = 0;
    ctx->net.last_join_ms = 0;
    ctx->net.state_enter_ms = 0;
    ctx->net.now_ms = 0;
    ctx->net.scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
    ctx->net.election_ms = UMESH_ELECTION_TIMEOUT_MS;
    ctx->net.routing_mode = UMESH_ROUTING_DISTANCE_VECTOR;
    ctx->net.gradient_beacon_ms = UMESH_GRADIENT_BEACON_MS;
    ctx->net.gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;
    ctx->net.last_gradient_beacon_ms = 0;
    ctx->net.last_election_result_ms = 0;
    net_dup_cache_reset(ctx);
#if UMESH_ENABLE_POWER_MANAGEMENT
    ctx->net.power_mode = UMESH_POWER_ACTIVE;
    ctx->net.light_sleep_interval_ms = UMESH_LIGHT_SLEEP_INTERVAL_MS;
    ctx->net.light_listen_window_ms = UMESH_LIGHT_LISTEN_WINDOW_MS;
    ctx->net.last_power_beacon_ms = 0;
#endif
    ctx->net.rx_cb = NULL;

    routing_init();
    mac_set_rx_callback(on_mac_rx);

    discovery_init(net_id, node_id, role);
    discovery_set_auto_timing(ctx->net.scan_ms, ctx->net.election_ms);
    discovery_enable_gradient(false, ctx->net.gradient_jitter_max_ms);

    return UMESH_OK;
}

void net_config_auto(uint32_t scan_ms, uint32_t election_ms,
                     const uint8_t local_mac[6])
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->net.scan_ms = (scan_ms == 0) ? UMESH_DISCOVER_TIMEOUT_MS : scan_ms;
    ctx->net.election_ms = (election_ms == 0) ? UMESH_ELECTION_TIMEOUT_MS : election_ms;
    discovery_set_auto_timing(ctx->net.scan_ms, ctx->net.election_ms);
    if (local_mac) {
        discovery_set_local_mac(local_mac);
    }
}

void net_config_routing(umesh_routing_mode_t routing_mode,
                        uint32_t gradient_beacon_ms,
                        uint32_t gradient_jitter_max_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->net.routing_mode = routing_mode;
    ctx->net.gradient_beacon_ms = (gradient_beacon_ms == 0)
        ? UMESH_GRADIENT_BEACON_MS : gradient_beacon_ms;
    ctx->net.gradient_jitter_max_ms = (gradient_jitter_max_ms == 0)
        ? UMESH_GRADIENT_JITTER_MAX_MS : gradient_jitter_max_ms;

    discovery_enable_gradient(ctx->net.routing_mode == UMESH_ROUTING_GRADIENT,
                              ctx->net.gradient_jitter_max_ms);
    if (ctx->net.routing_mode != UMESH_ROUTING_GRADIENT) {
        discovery_gradient_reset();
    }
}

void net_config_power(umesh_power_mode_t power_mode,
                      uint32_t light_interval_ms,
                      uint32_t light_listen_window_ms)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->net.power_mode = power_mode;
    ctx->net.light_sleep_interval_ms = (light_interval_ms == 0)
        ? UMESH_LIGHT_SLEEP_INTERVAL_MS : light_interval_ms;
    ctx->net.light_listen_window_ms = (light_listen_window_ms == 0)
        ? UMESH_LIGHT_LISTEN_WINDOW_MS : light_listen_window_ms;
    if (ctx->net.light_listen_window_ms > ctx->net.light_sleep_interval_ms) {
        ctx->net.light_listen_window_ms = ctx->net.light_sleep_interval_ms;
    }
#else
    UMESH_UNUSED(power_mode);
    UMESH_UNUSED(light_interval_ms);
    UMESH_UNUSED(light_listen_window_ms);
#endif
}

umesh_result_t net_join(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (ctx->net.state == UMESH_STATE_UNINIT) return UMESH_ERR_NOT_INIT;

    if (ctx->net.role_cfg != UMESH_ROLE_AUTO) {
        if (ctx->net.role == UMESH_ROLE_COORDINATOR) {
            ctx->net.state = UMESH_STATE_CONNECTED;
            ctx->net.node_id = UMESH_ADDR_COORDINATOR;
            mac_set_node_id(UMESH_ADDR_COORDINATOR);
            if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT) {
                discovery_gradient_set_distance(0);
                discovery_gradient_send_beacon(0);
                ctx->net.last_gradient_beacon_ms = ctx->net.now_ms;
            }
            return UMESH_OK;
        }

        if (ctx->net.node_id != UMESH_ADDR_UNASSIGNED) {
            ctx->net.state = UMESH_STATE_CONNECTED;
            ctx->net.role = ctx->net.role_cfg;
            mac_set_node_id(ctx->net.node_id);
            routing_add(UMESH_ADDR_COORDINATOR, UMESH_ADDR_COORDINATOR,
                        1, UMESH_RSSI_GOOD, 0);
            if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT &&
                ctx->net.role == UMESH_ROLE_COORDINATOR) {
                discovery_gradient_set_distance(0);
            }
            ctx->net.last_route_update_ms =
                (uint32_t)(0u - (UMESH_ROUTE_UPDATE_MS - 1000u));
            ctx->net.last_coord_seen_ms = ctx->net.now_ms;
            return UMESH_OK;
        }

        ctx->net.state = UMESH_STATE_JOINING;
        ctx->net.last_join_ms = ctx->net.now_ms;
        return discovery_join();
    }

    enter_scanning(ctx);
    return UMESH_OK;
}

umesh_result_t net_trigger_election(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (ctx->net.state == UMESH_STATE_UNINIT) return UMESH_ERR_NOT_INIT;
    ctx->net.role_cfg = UMESH_ROLE_AUTO;
    enter_scanning(ctx);
    return UMESH_OK;
}

uint8_t net_gradient_distance(void)
{
    return discovery_gradient_distance();
}

umesh_routing_mode_t net_get_routing_mode(void)
{
    return umesh_current_ctx()->net.routing_mode;
}

umesh_result_t net_gradient_refresh(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (ctx->net.routing_mode != UMESH_ROUTING_GRADIENT) {
        return UMESH_ERR_NOT_ROUTABLE;
    }
    if (ctx->net.state != UMESH_STATE_CONNECTED) {
        return UMESH_ERR_NOT_JOINED;
    }
    if (ctx->net.role != UMESH_ROLE_COORDINATOR) {
        return UMESH_ERR_INVALID_DST;
    }

    discovery_gradient_set_distance(0);
    ctx->net.last_gradient_beacon_ms = ctx->net.now_ms;
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
    umesh_ctx_t *ctx = umesh_current_ctx();
    discovery_leave();
    discovery_gradient_reset();
    net_dup_cache_reset(ctx);
    ctx->net.state = UMESH_STATE_DISCONNECTED;
    ctx->net.node_id = UMESH_ADDR_UNASSIGNED;
}

umesh_result_t net_route(umesh_frame_t *frame)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!frame) return UMESH_ERR_NULL_PTR;
    if (ctx->net.state != UMESH_STATE_CONNECTED) return UMESH_ERR_NOT_JOINED;

    frame->hop_count = UMESH_MAX_HOP_COUNT;
    frame->src = ctx->net.node_id;
    frame->link_src = ctx->net.node_id;
    if (frame->seq_num == 0 && ctx->cfg.security == UMESH_SEC_NONE) {
        frame->seq_num = next_seq(ctx);
    }
    if (frame->link_dst == 0) {
        frame->link_dst = frame->dst;
    }

    if (frame->dst == UMESH_ADDR_BROADCAST) {
        frame->link_dst = UMESH_ADDR_BROADCAST;
        return mac_send(frame);
    }

    if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT &&
        frame->dst == UMESH_ADDR_COORDINATOR) {
        uint8_t my_distance = discovery_gradient_distance();
        uint8_t next_hop = neighbor_find_uphill(my_distance);

        if (next_hop == UMESH_ADDR_BROADCAST) {
            return UMESH_ERR_NOT_ROUTABLE;
        }
        frame->link_dst = next_hop;
        return mac_send(frame);
    }

    return net_send_via_route(ctx, frame);
}

uint8_t net_get_node_id(void)
{
    return umesh_current_ctx()->net.node_id;
}

uint8_t net_get_state(void)
{
    return (uint8_t)umesh_current_ctx()->net.state;
}

umesh_role_t net_get_role(void)
{
    return umesh_current_ctx()->net.role;
}

bool net_is_coordinator(void)
{
    return umesh_current_ctx()->net.role == UMESH_ROLE_COORDINATOR;
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
    umesh_current_ctx()->net.rx_cb = cb;
}

void net_tick(uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t rebroadcast_distance = UINT8_MAX;

    ctx->net.now_ms = now_ms;
    routing_expire(now_ms);
    discovery_set_now(now_ms);
    if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT) {
        neighbor_expire(now_ms);
    }

    if (ctx->net.role_cfg == UMESH_ROLE_AUTO) {
        if (ctx->net.state == UMESH_STATE_SCANNING) {
            if (discovery_auto_seen_coordinator()) {
                discovery_auto_clear_scan_flag();
                ctx->net.role = UMESH_ROLE_ROUTER;
                discovery_set_role(UMESH_ROLE_ROUTER);
                ctx->net.state = UMESH_STATE_JOINING;
                ctx->net.state_enter_ms = now_ms;
                ctx->net.last_join_ms = now_ms;
                discovery_join();
            } else if (now_ms - ctx->net.state_enter_ms >= ctx->net.scan_ms) {
                ctx->net.state = UMESH_STATE_ELECTION;
                ctx->net.state_enter_ms = now_ms;
                discovery_auto_clear_election_flags();
                discovery_start_election();
            }
        } else if (ctx->net.state == UMESH_STATE_ELECTION) {
            if (discovery_auto_seen_election_result() ||
                discovery_auto_saw_lower_mac()) {
                ctx->net.role = UMESH_ROLE_ROUTER;
                discovery_set_role(UMESH_ROLE_ROUTER);
                if (discovery_get_node_id() != UMESH_ADDR_UNASSIGNED) {
                    ctx->net.node_id = discovery_get_node_id();
                    mac_set_node_id(ctx->net.node_id);
                    routing_add(UMESH_ADDR_COORDINATOR, UMESH_ADDR_COORDINATOR,
                                1, UMESH_RSSI_GOOD, now_ms);
                    ctx->net.state = UMESH_STATE_CONNECTED;
                    ctx->net.state_enter_ms = now_ms;
                    ctx->net.last_coord_seen_ms = now_ms;
                } else {
                    ctx->net.state = UMESH_STATE_JOINING;
                    ctx->net.state_enter_ms = now_ms;
                    ctx->net.last_join_ms = now_ms;
                    discovery_join();
                }
            } else if (now_ms - ctx->net.state_enter_ms >= ctx->net.election_ms) {
                ctx->net.role = UMESH_ROLE_COORDINATOR;
                ctx->net.node_id = UMESH_ADDR_COORDINATOR;
                discovery_auto_promote_to_coordinator();
                ctx->net.last_election_result_ms = now_ms;
                mac_set_node_id(UMESH_ADDR_COORDINATOR);
                if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT) {
                    discovery_gradient_set_distance(0);
                    discovery_gradient_send_beacon(0);
                    ctx->net.last_gradient_beacon_ms = now_ms;
                }
                ctx->net.state = UMESH_STATE_CONNECTED;
                ctx->net.state_enter_ms = now_ms;
                ctx->net.last_coord_seen_ms = now_ms;
            }
        }
    }

    if (ctx->net.state == UMESH_STATE_JOINING &&
        ctx->net.role != UMESH_ROLE_COORDINATOR) {
        if (now_ms - ctx->net.last_join_ms >= UMESH_JOIN_RETRY_MS) {
            discovery_join();
            ctx->net.last_join_ms = now_ms;
        }
    }

    if (ctx->net.state == UMESH_STATE_CONNECTED) {
#if UMESH_ENABLE_POWER_MANAGEMENT
        if (ctx->net.role == UMESH_ROLE_COORDINATOR &&
            ctx->net.power_mode != UMESH_POWER_ACTIVE &&
            now_ms - ctx->net.last_power_beacon_ms >= UMESH_POWER_BEACON_MS) {
            umesh_frame_t pframe;
            memset(&pframe, 0, sizeof(pframe));
            pframe.wire_version = UMESH_WIRE_VERSION;
            pframe.net_id = ctx->net.net_id;
            pframe.dst = UMESH_ADDR_BROADCAST;
            pframe.src = ctx->net.node_id;
            pframe.link_src = ctx->net.node_id;
            pframe.link_dst = UMESH_ADDR_BROADCAST;
            pframe.cmd = UMESH_CMD_POWER_BEACON;
            pframe.flags = UMESH_FLAG_PRIO_NORMAL;
            pframe.seq_num = next_seq(ctx);
            pframe.hop_count = UMESH_MAX_HOP_COUNT;
            pframe.payload_len = 6;
            pframe.payload[0] = (uint8_t)(ctx->net.light_sleep_interval_ms & 0xFF);
            pframe.payload[1] = (uint8_t)(ctx->net.light_sleep_interval_ms >> 8);
            pframe.payload[2] = (uint8_t)(ctx->net.light_listen_window_ms & 0xFF);
            pframe.payload[3] = (uint8_t)(ctx->net.light_listen_window_ms >> 8);
            pframe.payload[4] = 10;
            pframe.payload[5] = 0;
            mac_send(&pframe);
            ctx->net.last_power_beacon_ms = now_ms;
        }
#endif

        if (ctx->net.role_cfg == UMESH_ROLE_AUTO &&
            ctx->net.role == UMESH_ROLE_COORDINATOR &&
            now_ms - ctx->net.last_election_result_ms >= UMESH_ELECTION_RESULT_ANNOUNCE_MS) {
            discovery_broadcast_election_result();
            ctx->net.last_election_result_ms = now_ms;
        }

        if (ctx->net.routing_mode == UMESH_ROUTING_GRADIENT) {
            if (ctx->net.role == UMESH_ROLE_COORDINATOR) {
                discovery_gradient_set_distance(0);
                if ((ctx->net.last_gradient_beacon_ms == 0) ||
                    (now_ms - ctx->net.last_gradient_beacon_ms >= ctx->net.gradient_beacon_ms)) {
                    discovery_gradient_send_beacon(0);
                    ctx->net.last_gradient_beacon_ms = now_ms;
                }
            } else if (discovery_gradient_poll_rebroadcast(&rebroadcast_distance)) {
                discovery_gradient_send_beacon(rebroadcast_distance);
            }
        }

        if (now_ms - ctx->net.last_route_update_ms >= UMESH_ROUTE_UPDATE_MS) {
            umesh_frame_t frame;
            memset(&frame, 0, sizeof(frame));
            frame.wire_version = UMESH_WIRE_VERSION;
            frame.net_id = ctx->net.net_id;
            frame.dst = UMESH_ADDR_BROADCAST;
            frame.src = ctx->net.node_id;
            frame.link_src = ctx->net.node_id;
            frame.link_dst = UMESH_ADDR_BROADCAST;
            frame.cmd = UMESH_CMD_ROUTE_UPDATE;
            frame.flags = UMESH_FLAG_PRIO_NORMAL;
            frame.seq_num = next_seq(ctx);
            frame.hop_count = UMESH_MAX_HOP_COUNT;
            mac_send(&frame);
            ctx->net.last_route_update_ms = now_ms;
        }
    }

    if (ctx->net.role_cfg == UMESH_ROLE_AUTO &&
        ctx->net.state == UMESH_STATE_CONNECTED &&
        ctx->net.role != UMESH_ROLE_COORDINATOR) {
        if (now_ms - ctx->net.last_coord_seen_ms > UMESH_NODE_TIMEOUT_MS) {
            enter_scanning(ctx);
        }
    }

    if (ctx->net.role_cfg == UMESH_ROLE_AUTO &&
        ctx->net.state == UMESH_STATE_CONNECTED &&
        ctx->net.role == UMESH_ROLE_COORDINATOR &&
        discovery_get_role() != UMESH_ROLE_COORDINATOR) {
        ctx->net.role = UMESH_ROLE_ROUTER;
        ctx->net.node_id = discovery_get_node_id();
        mac_set_node_id(ctx->net.node_id);
        if (ctx->net.node_id == UMESH_ADDR_UNASSIGNED) {
            ctx->net.state = UMESH_STATE_JOINING;
            ctx->net.state_enter_ms = now_ms;
            ctx->net.last_join_ms = now_ms;
            discovery_join();
        } else {
            routing_add(UMESH_ADDR_COORDINATOR, UMESH_ADDR_COORDINATOR,
                        1, UMESH_RSSI_GOOD, now_ms);
        }
    }
}

void net_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    on_mac_rx((umesh_frame_t *)frame, rssi);
}
