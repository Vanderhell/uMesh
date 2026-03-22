#include "routing.h"
#include <string.h>

/*
 * Distance-vector routing table with RSSI-based metric.
 * Metric = hop_count * 10 + rssi_penalty
 * (lower = better)
 */

static umesh_route_entry_t s_table[UMESH_MAX_ROUTES];

void routing_init(void)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        s_table[i].valid = false;
    }
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

/* Find an existing entry for dst_node. Returns NULL if not found. */
static umesh_route_entry_t *find_entry(uint8_t dst)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        if (s_table[i].valid && s_table[i].dst_node == dst) {
            return &s_table[i];
        }
    }
    return NULL;
}

bool routing_add(uint8_t dst, uint8_t next_hop,
                 uint8_t hops, int8_t rssi,
                 uint32_t now_ms)
{
    umesh_route_entry_t *entry;
    uint8_t i;
    uint8_t metric = routing_metric(hops, rssi);

    /* Update if already present */
    entry = find_entry(dst);
    if (entry) {
        if (metric < entry->metric) {
            entry->next_hop    = next_hop;
            entry->hop_count   = hops;
            entry->last_rssi   = rssi;
            entry->metric      = metric;
            entry->last_seen_ms = now_ms;
        } else {
            /* Keep existing route but refresh timestamp */
            entry->last_seen_ms = now_ms;
        }
        return true;
    }

    /* Find free slot */
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        if (!s_table[i].valid) {
            s_table[i].valid        = true;
            s_table[i].dst_node     = dst;
            s_table[i].next_hop     = next_hop;
            s_table[i].hop_count    = hops;
            s_table[i].last_rssi    = rssi;
            s_table[i].metric       = metric;
            s_table[i].last_seen_ms = now_ms;
            return true;
        }
    }
    return false; /* table full */
}

bool routing_find(uint8_t dst, umesh_route_entry_t *out)
{
    umesh_route_entry_t *entry = find_entry(dst);
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
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        if (!s_table[i].valid) continue;
        if (now_ms - s_table[i].last_seen_ms > UMESH_NODE_TIMEOUT_MS) {
            s_table[i].valid = false;
        }
    }
}

void routing_remove(uint8_t dst)
{
    umesh_route_entry_t *entry = find_entry(dst);
    if (entry) entry->valid = false;
}
