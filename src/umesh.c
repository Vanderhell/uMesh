#include "../include/umesh.h"
#include "phy/phy.h"
#include "mac/mac.h"
#include "net/net.h"
#include "sec/sec.h"
#include <string.h>

/* Stub — will be fully implemented in Step 14 */

static bool         s_initialized = false;
static umesh_cfg_t  s_cfg;
static umesh_stats_t s_stats;

static void (*s_rx_cb)(umesh_pkt_t *pkt)          = NULL;
static void (*s_cmd_cb[256])(umesh_pkt_t *pkt);

umesh_result_t umesh_init(const umesh_cfg_t *cfg)
{
    if (!cfg) return UMESH_ERR_NULL_PTR;
    s_cfg = *cfg;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_cmd_cb, 0, sizeof(s_cmd_cb));
    s_initialized = true;
    return UMESH_OK;
}

umesh_result_t umesh_start(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    return UMESH_OK;
}

umesh_result_t umesh_stop(void)
{
    return UMESH_OK;
}

umesh_result_t umesh_reset(void)
{
    s_initialized = false;
    return UMESH_OK;
}

umesh_result_t umesh_send(uint8_t dst, uint8_t cmd,
                          const void *payload, uint8_t len,
                          uint8_t flags)
{
    (void)dst; (void)cmd; (void)payload; (void)len; (void)flags;
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    return UMESH_ERR_NOT_INIT;
}

umesh_result_t umesh_send_cmd(uint8_t dst, uint8_t cmd, uint8_t flags)
{
    return umesh_send(dst, cmd, NULL, 0, flags);
}

umesh_result_t umesh_broadcast(uint8_t cmd,
                                const void *payload, uint8_t len)
{
    return umesh_send(UMESH_ADDR_BROADCAST, cmd, payload, len, 0);
}

umesh_result_t umesh_send_raw(uint8_t dst,
                               const void *payload, uint8_t len,
                               uint8_t flags)
{
    return umesh_send(dst, UMESH_CMD_RAW, payload, len, flags);
}

void umesh_on_receive(void (*cb)(umesh_pkt_t *pkt))
{
    s_rx_cb = cb;
}

void umesh_on_cmd(uint8_t cmd, void (*cb)(umesh_pkt_t *pkt))
{
    s_cmd_cb[cmd] = cb;
}

umesh_info_t umesh_get_info(void)
{
    umesh_info_t info;
    info.node_id    = s_cfg.node_id;
    info.net_id     = s_cfg.net_id;
    info.role       = s_cfg.role;
    info.state      = UMESH_STATE_UNINIT;
    info.node_count = 0;
    info.channel    = s_cfg.channel;
    return info;
}

umesh_stats_t umesh_get_stats(void)
{
    return s_stats;
}

const char *umesh_err_str(umesh_result_t err)
{
    switch (err) {
    case UMESH_OK:               return "OK";
    case UMESH_ERR_NO_ACK:       return "NO_ACK";
    case UMESH_ERR_CHANNEL_BUSY: return "CHANNEL_BUSY";
    case UMESH_ERR_MAX_RETRIES:  return "MAX_RETRIES";
    case UMESH_ERR_NOT_ROUTABLE: return "NOT_ROUTABLE";
    case UMESH_ERR_NOT_JOINED:   return "NOT_JOINED";
    case UMESH_ERR_TIMEOUT:      return "TIMEOUT";
    case UMESH_ERR_INVALID_DST:  return "INVALID_DST";
    case UMESH_ERR_TOO_LONG:     return "TOO_LONG";
    case UMESH_ERR_NULL_PTR:     return "NULL_PTR";
    case UMESH_ERR_NOT_INIT:     return "NOT_INIT";
    case UMESH_ERR_HARDWARE:     return "HARDWARE";
    case UMESH_ERR_MIC_FAIL:     return "MIC_FAIL";
    case UMESH_ERR_REPLAY:       return "REPLAY";
    default:                     return "UNKNOWN";
    }
}
