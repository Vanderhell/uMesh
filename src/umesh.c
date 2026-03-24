#include "../include/umesh.h"
#include "phy/phy.h"
#include "phy/phy_hal.h"
#include "mac/mac.h"
#include "net/net.h"
#include "power/power.h"
#include "sec/sec.h"
#include <string.h>
#ifdef UMESH_PORT_ESP32
#include "esp_wifi.h"
#endif

/* ── State ─────────────────────────────────────────────── */

static bool          s_initialized = false;
static umesh_cfg_t   s_cfg;
static umesh_stats_t s_stats;

/* User-registered callbacks */
static void (*s_rx_cb)(umesh_pkt_t *pkt)      = NULL;
static void (*s_cmd_cb[256])(umesh_pkt_t *pkt);
static bool s_role_notified = false;
static bool s_join_notified = false;
static bool s_gradient_notified = false;
static umesh_role_t s_last_role = UMESH_ROLE_END_NODE;
static uint8_t s_last_node_id = UMESH_ADDR_UNASSIGNED;
static uint8_t s_last_gradient_distance = UINT8_MAX;

static void notify_state_changes(void)
{
    umesh_role_t role = net_get_role();
    uint8_t node_id = net_get_node_id();

    if (s_cfg.on_role_elected && (!s_role_notified || role != s_last_role)) {
        s_cfg.on_role_elected(role);
        s_role_notified = true;
        s_last_role = role;
    }

    if (s_cfg.on_joined) {
        if ((!s_join_notified || node_id != s_last_node_id) &&
            node_id != UMESH_ADDR_UNASSIGNED) {
            s_cfg.on_joined(node_id);
            s_join_notified = true;
        }
    }

    if (s_cfg.on_gradient_ready &&
        s_cfg.routing == UMESH_ROUTING_GRADIENT) {
        uint8_t distance = net_gradient_distance();
        if (distance != UINT8_MAX &&
            (!s_gradient_notified || distance != s_last_gradient_distance)) {
            s_cfg.on_gradient_ready(distance);
            s_gradient_notified = true;
        }
        s_last_gradient_distance = distance;
    }

    s_last_node_id = node_id;
}

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
    uint8_t local_mac[6];
    bool have_local_mac = false;

    if (!cfg) return UMESH_ERR_NULL_PTR;

    s_cfg = *cfg;
    if (s_cfg.channel == 0) s_cfg.channel = UMESH_DEFAULT_CHANNEL;
    if (s_cfg.tx_power == 0) s_cfg.tx_power = UMESH_TX_POWER_DEFAULT;
    if (s_cfg.node_id == 0) s_cfg.node_id = UMESH_ADDR_UNASSIGNED;
    if ((int)s_cfg.security < (int)UMESH_SEC_NONE ||
        (int)s_cfg.security > (int)UMESH_SEC_FULL) {
        s_cfg.security = UMESH_SEC_FULL;
    }
    if ((int)s_cfg.role < (int)UMESH_ROLE_COORDINATOR ||
        (int)s_cfg.role > (int)UMESH_ROLE_AUTO) {
        s_cfg.role = UMESH_ROLE_AUTO;
    }
    if ((int)s_cfg.routing < (int)UMESH_ROUTING_DISTANCE_VECTOR ||
        (int)s_cfg.routing > (int)UMESH_ROUTING_GRADIENT) {
        s_cfg.routing = UMESH_ROUTING_DISTANCE_VECTOR;
    }
    if (s_cfg.gradient_beacon_ms == 0) {
        s_cfg.gradient_beacon_ms = UMESH_GRADIENT_BEACON_MS;
    }
    if (s_cfg.gradient_jitter_max_ms == 0) {
        s_cfg.gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;
    }
    if ((int)s_cfg.power_mode < (int)UMESH_POWER_ACTIVE ||
        (int)s_cfg.power_mode > (int)UMESH_POWER_DEEP) {
        s_cfg.power_mode = UMESH_POWER_ACTIVE;
    }
    if (s_cfg.light_sleep_interval_ms == 0) {
        s_cfg.light_sleep_interval_ms = UMESH_LIGHT_SLEEP_INTERVAL_MS;
    }
    if (s_cfg.light_listen_window_ms == 0) {
        s_cfg.light_listen_window_ms = UMESH_LIGHT_LISTEN_WINDOW_MS;
    }
    if (s_cfg.deep_sleep_tx_interval_ms == 0) {
        s_cfg.deep_sleep_tx_interval_ms = UMESH_DEEP_SLEEP_TX_INTERVAL_MS;
    }
    if (s_cfg.scan_ms == 0) s_cfg.scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
    if (s_cfg.election_ms == 0) s_cfg.election_ms = UMESH_ELECTION_TIMEOUT_MS;

    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_cmd_cb, 0, sizeof(s_cmd_cb));
    s_rx_cb       = NULL;
    s_initialized = false;
    s_role_notified = false;
    s_join_notified = false;
    s_gradient_notified = false;
    s_last_role = UMESH_ROLE_END_NODE;
    s_last_node_id = UMESH_ADDR_UNASSIGNED;
    s_last_gradient_distance = UINT8_MAX;

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
    r = net_init(s_cfg.net_id, s_cfg.node_id, s_cfg.role);
    if (r != UMESH_OK) return r;

    if (s_cfg.role == UMESH_ROLE_AUTO) {
#ifdef UMESH_PORT_ESP32
        if (esp_wifi_get_mac(WIFI_IF_STA, local_mac) == ESP_OK) {
            have_local_mac = true;
        }
#endif
        if (!have_local_mac) {
            local_mac[0] = 0xAC;
            local_mac[1] = 0x00;
            local_mac[2] = s_cfg.net_id;
            local_mac[3] = s_cfg.node_id;
            local_mac[4] = s_cfg.channel;
            local_mac[5] = 0x01;
            have_local_mac = true;
        }
    }

    net_config_auto(s_cfg.scan_ms, s_cfg.election_ms,
                    have_local_mac ? local_mac : NULL);
    net_config_routing(s_cfg.routing, s_cfg.gradient_beacon_ms,
                       s_cfg.gradient_jitter_max_ms);
    net_config_power(s_cfg.power_mode, s_cfg.light_sleep_interval_ms,
                     s_cfg.light_listen_window_ms);
    power_init(s_cfg.power_mode, s_cfg.light_sleep_interval_ms,
               s_cfg.light_listen_window_ms, s_cfg.deep_sleep_tx_interval_ms,
               s_cfg.on_sleep, s_cfg.on_wake);

    net_set_rx_callback(on_net_rx);

    s_initialized = true;
    return UMESH_OK;
}

umesh_result_t umesh_start(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    {
        umesh_result_t r = net_join();
        if (r == UMESH_OK) notify_state_changes();
        return r;
    }
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
    info.role       = net_get_role();
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

void umesh_tick(uint32_t now_ms)
{
    if (!s_initialized) return;
    net_tick(now_ms);
    power_tick(now_ms, net_get_role());
    notify_state_changes();
}

umesh_role_t umesh_get_role(void)
{
    return net_get_role();
}

bool umesh_is_coordinator(void)
{
    return net_is_coordinator();
}

umesh_result_t umesh_trigger_election(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    return net_trigger_election();
}

uint8_t umesh_gradient_distance(void)
{
    return net_gradient_distance();
}

umesh_routing_mode_t umesh_get_routing_mode(void)
{
    return net_get_routing_mode();
}

umesh_result_t umesh_gradient_refresh(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    return net_gradient_refresh();
}

uint8_t umesh_get_neighbor_count(void)
{
    return net_get_neighbor_count();
}

umesh_neighbor_t umesh_get_neighbor(uint8_t index)
{
    umesh_neighbor_t out;
    memset(&out, 0, sizeof(out));
    out.distance = UINT8_MAX;
    if (!net_get_neighbor(index, &out)) {
        out.node_id = UMESH_ADDR_BROADCAST;
        out.rssi = -127;
    }
    return out;
}

umesh_result_t umesh_set_power_mode(umesh_power_mode_t mode)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    s_cfg.power_mode = mode;
    net_config_power(mode, s_cfg.light_sleep_interval_ms,
                     s_cfg.light_listen_window_ms);
    return power_set_mode(mode);
}

umesh_power_mode_t umesh_get_power_mode(void)
{
    return power_get_mode();
}

umesh_result_t umesh_deep_sleep_cycle(void)
{
    if (!s_initialized) return UMESH_ERR_NOT_INIT;
    return power_deep_sleep_cycle(s_cfg.routing, net_get_role());
}

float umesh_estimate_current_ma(void)
{
    return power_estimate_current_ma();
}

umesh_power_stats_t umesh_get_power_stats(void)
{
    return power_get_stats();
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
