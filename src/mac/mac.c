#include "mac.h"
#include "cca.h"
#include "../phy/phy.h"
#include <stdlib.h>
#include <string.h>

/* Stub — will be fully implemented in Step 10 */

static void (*s_rx_cb)(umesh_frame_t *frame, int8_t rssi) = NULL;
static mac_stats_t s_stats;
static uint8_t s_node_id = 0;

umesh_result_t mac_init(uint8_t node_id)
{
    s_node_id = node_id;
    cca_init();
    memset(&s_stats, 0, sizeof(s_stats));
    return UMESH_OK;
}

umesh_result_t mac_send(umesh_frame_t *frame)
{
    UMESH_UNUSED(frame);
    return UMESH_ERR_NOT_INIT;
}

void mac_set_rx_callback(void (*cb)(umesh_frame_t *frame, int8_t rssi))
{
    s_rx_cb = cb;
}

bool mac_channel_is_free(void)
{
    return cca_channel_free();
}

mac_stats_t mac_get_stats(void)
{
    return s_stats;
}

void mac_on_raw_rx(const uint8_t *buf, uint8_t len, int8_t rssi)
{
    UMESH_UNUSED(buf);
    UMESH_UNUSED(len);
    UMESH_UNUSED(rssi);
}
