#include "net.h"
#include "discovery.h"

/* Stub — will be implemented in Steps 12 & 13 */
static uint8_t s_node_id  = 0;
static uint8_t s_net_id   = 0;
static umesh_role_t s_role = UMESH_ROLE_END_NODE;
static umesh_state_t s_state = UMESH_STATE_UNINIT;

umesh_result_t net_init(uint8_t net_id, uint8_t node_id,
                        umesh_role_t role)
{
    s_net_id   = net_id;
    s_node_id  = node_id;
    s_role     = role;
    s_state    = UMESH_STATE_SCANNING;
    routing_init();
    return discovery_init(net_id, node_id, role);
}

umesh_result_t net_join(void)
{
    return discovery_join();
}

void net_leave(void)
{
    discovery_leave();
    s_state = UMESH_STATE_DISCONNECTED;
}

umesh_result_t net_route(umesh_frame_t *frame)
{
    UMESH_UNUSED(frame);
    return UMESH_ERR_NOT_INIT;
}

uint8_t net_get_node_id(void)
{
    return s_node_id;
}

uint8_t net_get_state(void)
{
    return (uint8_t)s_state;
}

uint8_t net_get_node_count(void)
{
    return 0;
}

void net_tick(uint32_t now_ms)
{
    routing_expire(now_ms);
}

void net_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    discovery_on_frame(frame, rssi);
}
