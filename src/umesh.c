#include "../include/umesh.h"
#include "phy/phy.h"
#include "phy/phy_hal.h"
#include "mac/mac.h"
#include "net/net.h"
#include "sec/sec.h"
#include <string.h>

/* ── State ─────────────────────────────────────────────── */

static bool          s_initialized = false;
static umesh_cfg_t   s_cfg;
static umesh_stats_t s_stats;

/* User-registered callbacks */
static void (*s_rx_cb)(umesh_pkt_t *pkt)      = NULL;
static void (*s_cmd_cb[256])(umesh_pkt_t *pkt);

/* ── Internal RX dispatch ──────────────────────────────── */

static void on_net_rx(const umesh_frame_t *frame, int8_t rssi)
{
    umesh_pkt_t pkt;

    if (!frame) return;

    pkt.src         = frame->src;
    pkt.dst         = frame->dst;
    pkt.cmd         = frame->cmd;
    pkt.payload     = (uint8_t *)frame->payload;
    pkt.payload_len = frame->payload_len;
    pkt.rssi        = rssi;

    s_stats.rx_count++;

    /* Dispatch to command handler */
    if (s_cmd_cb[frame->cmd]) {
        s_cmd_cb[frame->cmd](&pkt);
    }

    /* Dispatch to general receive handler */
    if (s_rx_cb) {
        s_rx_cb(&pkt);
    }
}

/* ── Public API ────────────────────────────────────────── */

umesh_result_t umesh_init(const umesh_cfg_t *cfg)
{
    umesh_phy_cfg_t phy_cfg;
    umesh_result_t  r;

    if (!cfg) return UMESH_ERR_NULL_PTR;

    s_cfg = *cfg;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_cmd_cb, 0, sizeof(s_cmd_cb));
    s_rx_cb       = NULL;
    s_initialized = false;

    /* PHY */
    phy_cfg.channel  = cfg->channel;
    phy_cfg.tx_power = cfg->tx_power;
    phy_cfg.net_id   = cfg->net_id;
    r = phy_hal_init(&phy_cfg);
    if (r != UMESH_OK) return r;

    /* MAC */
    r = mac_init(cfg->node_id);
    if (r != UMESH_OK) return r;

    /* Security */
    if (cfg->master_key) {
        r = sec_init(cfg->master_key, cfg->net_id, cfg->security);
        if (r != UMESH_OK) return r;
    }

    /* Network */
    r = net_init(cfg->net_id, cfg->node_id, cfg->role);
    if (r != UMESH_OK) return r;

    net_set_rx_callback(on_net_rx);

    s_initialized = true;
    return UMESH_OK;
}

umesh_result_t umesh_start(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    return net_join();
}

umesh_result_t umesh_stop(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    net_leave();
    return UMESH_OK;
}

umesh_result_t umesh_reset(void)
{
    net_leave();
    s_initialized = false;
    memset(&s_stats, 0, sizeof(s_stats));
    return UMESH_OK;
}

umesh_result_t umesh_send(uint8_t dst, uint8_t cmd,
                          const void *payload, uint8_t len,
                          uint8_t flags)
{
    umesh_frame_t frame;
    umesh_result_t r;

    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    if (len > UMESH_MAX_PAYLOAD) return UMESH_ERR_TOO_LONG;

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = s_cfg.net_id;
    frame.dst         = dst;
    frame.src         = net_get_node_id();
    frame.flags       = flags;
    frame.cmd         = cmd;
    frame.payload_len = len;
    frame.hop_count   = UMESH_MAX_HOP_COUNT;
    if (len > 0 && payload) {
        memcpy(frame.payload, payload, len);
    }

    /* Encrypt if security is enabled */
    if (s_cfg.security != UMESH_SEC_NONE &&
        s_cfg.master_key) {
        r = sec_encrypt_frame(&frame);
        if (r != UMESH_OK) return r;
    }

    r = net_route(&frame);
    if (r == UMESH_OK) s_stats.tx_count++;
    return r;
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
    info.node_id    = net_get_node_id();
    info.net_id     = s_cfg.net_id;
    info.role       = s_cfg.role;
    info.state      = (umesh_state_t)net_get_state();
    info.node_count = net_get_node_count();
    info.channel    = s_cfg.channel;
    return info;
}

umesh_stats_t umesh_get_stats(void)
{
    mac_stats_t mac = mac_get_stats();
    s_stats.ack_count   = mac.ack_count;
    s_stats.retry_count = mac.retry_count;
    s_stats.drop_count  = mac.drop_count;
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
