#include "discovery.h"

/* Stub — will be implemented in Step 12 */
umesh_result_t discovery_init(uint8_t net_id, uint8_t node_id,
                               umesh_role_t role)
{
    UMESH_UNUSED(net_id);
    UMESH_UNUSED(node_id);
    UMESH_UNUSED(role);
    return UMESH_OK;
}

umesh_result_t discovery_join(void)
{
    return UMESH_ERR_NOT_INIT;
}

void discovery_leave(void)
{
    /* stub */
}

void discovery_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    UMESH_UNUSED(frame);
    UMESH_UNUSED(rssi);
}
