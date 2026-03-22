#include "routing.h"

/* Stub — will be implemented in Step 11 */
static umesh_route_entry_t s_table[UMESH_MAX_ROUTES];

void routing_init(void)
{
    uint8_t i;
    for (i = 0; i < UMESH_MAX_ROUTES; i++) {
        s_table[i].valid = false;
    }
}

bool routing_add(uint8_t dst, uint8_t next_hop,
                 uint8_t hops, int8_t rssi,
                 uint32_t now_ms)
{
    UMESH_UNUSED(dst);
    UMESH_UNUSED(next_hop);
    UMESH_UNUSED(hops);
    UMESH_UNUSED(rssi);
    UMESH_UNUSED(now_ms);
    return false;
}

bool routing_find(uint8_t dst, umesh_route_entry_t *out)
{
    UMESH_UNUSED(dst);
    UMESH_UNUSED(out);
    return false;
}

void routing_update(uint8_t dst, uint8_t next_hop,
                    uint8_t hops, int8_t rssi,
                    uint32_t now_ms)
{
    UMESH_UNUSED(dst);
    UMESH_UNUSED(next_hop);
    UMESH_UNUSED(hops);
    UMESH_UNUSED(rssi);
    UMESH_UNUSED(now_ms);
}

void routing_expire(uint32_t now_ms)
{
    UMESH_UNUSED(now_ms);
}

uint8_t routing_metric(uint8_t hops, int8_t rssi)
{
    UMESH_UNUSED(hops);
    UMESH_UNUSED(rssi);
    return 255;
}

void routing_remove(uint8_t dst)
{
    UMESH_UNUSED(dst);
}
