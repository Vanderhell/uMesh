#ifndef UMESH_ROUTING_H
#define UMESH_ROUTING_H

#include "../common/defs.h"

typedef struct {
    uint8_t  dst_node;
    uint8_t  next_hop;
    uint8_t  hop_count;
    int8_t   last_rssi;
    uint8_t  metric;
    uint32_t last_seen_ms;
    bool     valid;
} umesh_route_entry_t;

void    routing_init(void);
bool    routing_add(uint8_t dst, uint8_t next_hop,
                    uint8_t hops, int8_t rssi,
                    uint32_t now_ms);
bool    routing_find(uint8_t dst, umesh_route_entry_t *out);
void    routing_update(uint8_t dst, uint8_t next_hop,
                       uint8_t hops, int8_t rssi,
                       uint32_t now_ms);
void    routing_expire(uint32_t now_ms);
uint8_t routing_metric(uint8_t hops, int8_t rssi);
void    routing_remove(uint8_t dst);

void    neighbor_init(void);
void    neighbor_update(uint8_t node_id, uint8_t distance,
                        int8_t rssi, uint32_t now_ms);
uint8_t neighbor_find_uphill(uint8_t my_distance);
void    neighbor_expire(uint32_t now_ms);
uint8_t neighbor_count(void);
bool    neighbor_get(uint8_t index, umesh_neighbor_t *out);

#endif /* UMESH_ROUTING_H */
