#include "routing.h"
#include "../context.h"
#include <string.h>

static umesh_route_entry_t *find_entry(umesh_ctx_t *ctx, uint8_t dst)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        if (ctx->routing.table[i].valid && ctx->routing.table[i].dst_node == dst) {
            return &ctx->routing.table[i];
        }
    }
    return NULL;
}

void routing_init(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        ctx->routing.table[i].valid = false;
    }
    neighbor_init();
}

uint8_t routing_metric(uint8_t hops, int8_t rssi)
{
    uint8_t rssi_penalty;
    if      (rssi > UMESH_RSSI_EXCELLENT) rssi_penalty = 0;
    else if (rssi > UMESH_RSSI_GOOD)      rssi_penalty = 2;
    else if (rssi > UMESH_RSSI_FAIR)      rssi_penalty = 5;
    else                                   rssi_penalty = 10;
    return (uint8_t)(hops * 10u + rssi_penalty);
}

bool routing_add(uint8_t dst, uint8_t next_hop,
                 uint8_t hops, int8_t rssi,
                 uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    umesh_route_entry_t *entry;
    uint8_t i;
    uint8_t metric = routing_metric(hops, rssi);

    entry = find_entry(ctx, dst);
    if (entry) {
        if (entry->next_hop == next_hop) {
            if (metric < entry->metric) {
                entry->next_hop    = next_hop;
                entry->hop_count   = hops;
                entry->last_rssi   = rssi;
                entry->metric      = metric;
            }
            entry->last_seen_ms = now_ms;
        } else if (metric < entry->metric) {
            entry->next_hop    = next_hop;
            entry->hop_count   = hops;
            entry->last_rssi   = rssi;
            entry->metric      = metric;
            entry->last_seen_ms = now_ms;
        }
        return true;
    }

    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        if (!ctx->routing.table[i].valid) {
            ctx->routing.table[i].valid        = true;
            ctx->routing.table[i].dst_node     = dst;
            ctx->routing.table[i].next_hop     = next_hop;
            ctx->routing.table[i].hop_count    = hops;
            ctx->routing.table[i].last_rssi    = rssi;
            ctx->routing.table[i].metric       = metric;
            ctx->routing.table[i].last_seen_ms = now_ms;
            return true;
        }
    }
    return false;
}

bool routing_find(uint8_t dst, umesh_route_entry_t *out)
{
    umesh_route_entry_t *entry = find_entry(umesh_current_ctx(), dst);
    if (!entry) return false;
    if (out) *out = *entry;
    return true;
}

void routing_update(uint8_t dst, uint8_t next_hop,
                    uint8_t hops, int8_t rssi,
                    uint32_t now_ms)
{
    routing_add(dst, next_hop, hops, rssi, now_ms);
}

void routing_expire(uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        if (!ctx->routing.table[i].valid) continue;
        if (now_ms >= ctx->routing.table[i].last_seen_ms &&
            now_ms - ctx->routing.table[i].last_seen_ms > UMESH_NODE_TIMEOUT_MS) {
            ctx->routing.table[i].valid = false;
        }
    }
}

void routing_remove(uint8_t dst)
{
    umesh_route_entry_t *entry = find_entry(umesh_current_ctx(), dst);
    if (entry) entry->valid = false;
}

void neighbor_init(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    for (i = 0; i < UMESH_MAX_NEIGHBORS; i++) {
        ctx->routing.neighbors[i].node_id = UMESH_ADDR_BROADCAST;
        ctx->routing.neighbors[i].distance = UINT8_MAX;
        ctx->routing.neighbors[i].rssi = -127;
        ctx->routing.neighbors[i].last_seen_ms = 0;
    }
}

void neighbor_update(uint8_t node_id, uint8_t distance,
                     int8_t rssi, uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    int free_idx = -1;

    if (node_id == UMESH_ADDR_BROADCAST ||
        node_id == UMESH_ADDR_UNASSIGNED) {
        return;
    }

    for (i = 0; i < UMESH_MAX_NEIGHBORS; i++) {
        if (ctx->routing.neighbors[i].node_id == node_id) {
            ctx->routing.neighbors[i].distance = distance;
            ctx->routing.neighbors[i].rssi = rssi;
            ctx->routing.neighbors[i].last_seen_ms = now_ms;
            return;
        }
        if (free_idx < 0 && ctx->routing.neighbors[i].node_id == UMESH_ADDR_BROADCAST) {
            free_idx = (int)i;
        }
    }

    if (free_idx >= 0) {
        ctx->routing.neighbors[free_idx].node_id = node_id;
        ctx->routing.neighbors[free_idx].distance = distance;
        ctx->routing.neighbors[free_idx].rssi = rssi;
        ctx->routing.neighbors[free_idx].last_seen_ms = now_ms;
    }
}

uint8_t neighbor_find_uphill(uint8_t my_distance)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    bool found = false;
    uint8_t best_node = UMESH_ADDR_BROADCAST;
    uint8_t best_distance = UINT8_MAX;
    int8_t best_rssi = -127;

    if (my_distance == UINT8_MAX) {
        return UMESH_ADDR_BROADCAST;
    }

    for (i = 0; i < UMESH_MAX_NEIGHBORS; i++) {
        uint8_t n_distance = ctx->routing.neighbors[i].distance;
        int8_t n_rssi = ctx->routing.neighbors[i].rssi;
        if (ctx->routing.neighbors[i].node_id == UMESH_ADDR_BROADCAST) continue;
        if (n_distance >= my_distance) continue;

        if (!found ||
            n_distance < best_distance ||
            (n_distance == best_distance && n_rssi > best_rssi)) {
            found = true;
            best_node = ctx->routing.neighbors[i].node_id;
            best_distance = n_distance;
            best_rssi = n_rssi;
        }
    }

    return best_node;
}

void neighbor_expire(uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    for (i = 0; i < UMESH_MAX_NEIGHBORS; i++) {
        if (ctx->routing.neighbors[i].node_id == UMESH_ADDR_BROADCAST) continue;
        if (now_ms >= ctx->routing.neighbors[i].last_seen_ms &&
            now_ms - ctx->routing.neighbors[i].last_seen_ms > UMESH_NEIGHBOR_TIMEOUT_MS) {
            ctx->routing.neighbors[i].node_id = UMESH_ADDR_BROADCAST;
            ctx->routing.neighbors[i].distance = UINT8_MAX;
            ctx->routing.neighbors[i].rssi = -127;
            ctx->routing.neighbors[i].last_seen_ms = 0;
        }
    }
}

uint8_t neighbor_count(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    uint8_t count = 0;
    for (i = 0; i < UMESH_MAX_NEIGHBORS; i++) {
        if (ctx->routing.neighbors[i].node_id != UMESH_ADDR_BROADCAST) {
            count++;
        }
    }
    return count;
}

bool neighbor_get(uint8_t index, umesh_neighbor_t *out)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t i;
    uint8_t seen = 0;
    if (!out) return false;

    for (i = 0; i < UMESH_MAX_NEIGHBORS; i++) {
        if (ctx->routing.neighbors[i].node_id == UMESH_ADDR_BROADCAST) continue;
        if (seen == index) {
            *out = ctx->routing.neighbors[i];
            return true;
        }
        seen++;
    }
    return false;
}
