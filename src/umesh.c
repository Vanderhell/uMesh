#include "../include/umesh.h"
#include "context.h"
#include "phy/phy.h"
#include "phy/phy_hal.h"
#include "mac/mac.h"
#include "net/net.h"
#if UMESH_ENABLE_POWER_MANAGEMENT
#include "power/power.h"
#endif
#include "sec/sec.h"
#include <string.h>
#ifdef UMESH_PORT_ESP32
#include "esp_wifi.h"
#endif

static umesh_ctx_t *ctx_or_default(umesh_ctx_t *ctx)
{
    if (ctx) {
        return ctx;
    }
    return umesh_current_ctx();
}

static void notify_state_changes(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    umesh_role_t role = net_get_role();
    uint8_t node_id = net_get_node_id();

    if (ctx->cfg.on_role_elected && (!ctx->role_notified || role != ctx->last_role)) {
        ctx->cfg.on_role_elected(role);
        ctx->role_notified = true;
        ctx->last_role = role;
    }

    if (ctx->cfg.on_joined) {
        if ((!ctx->join_notified || node_id != ctx->last_node_id) &&
            node_id != UMESH_ADDR_UNASSIGNED) {
            ctx->cfg.on_joined(node_id);
            ctx->join_notified = true;
        }
    }

    if (ctx->cfg.on_gradient_ready &&
        ctx->cfg.routing == UMESH_ROUTING_GRADIENT) {
        uint8_t distance = net_gradient_distance();
        if (distance != UINT8_MAX &&
            (!ctx->gradient_notified || distance != ctx->last_gradient_distance)) {
            ctx->cfg.on_gradient_ready(distance);
            ctx->gradient_notified = true;
        }
        ctx->last_gradient_distance = distance;
    }

    ctx->last_node_id = node_id;
}

static void on_net_rx(const umesh_frame_t *frame, int8_t rssi)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    umesh_pkt_t pkt;

    if (!frame) return;

    pkt.src         = frame->src;
    pkt.dst         = frame->dst;
    pkt.cmd         = frame->cmd;
    pkt.payload     = (uint8_t *)frame->payload;
    pkt.payload_len = frame->payload_len;
    pkt.rssi        = rssi;

    ctx->stats.rx_count++;

    if (ctx->cmd_cb[frame->cmd]) {
        ctx->cmd_cb[frame->cmd](&pkt);
    }

    if (ctx->rx_cb) {
        ctx->rx_cb(&pkt);
    }
}

static umesh_result_t validate_cfg(const umesh_cfg_t *cfg)
{
    if (!cfg) return UMESH_ERR_NULL_PTR;
    if (cfg->channel == 0) return UMESH_ERR_INVALID_DST;
    if (cfg->tx_power > UMESH_TX_POWER_MAX) return UMESH_ERR_INVALID_DST;
    if (cfg->net_id == 0) return UMESH_ERR_INVALID_DST;
    if ((int)cfg->role < (int)UMESH_ROLE_COORDINATOR ||
        (int)cfg->role > (int)UMESH_ROLE_AUTO) {
        return UMESH_ERR_INVALID_DST;
    }
    if ((int)cfg->routing < (int)UMESH_ROUTING_DISTANCE_VECTOR ||
        (int)cfg->routing > (int)UMESH_ROUTING_GRADIENT) {
        return UMESH_ERR_INVALID_DST;
    }
    if ((int)cfg->power_mode < (int)UMESH_POWER_ACTIVE ||
        (int)cfg->power_mode > (int)UMESH_POWER_DEEP) {
        return UMESH_ERR_INVALID_DST;
    }
    if ((int)cfg->security < (int)UMESH_SEC_NONE ||
        (int)cfg->security > (int)UMESH_SEC_FULL) {
        return UMESH_ERR_INVALID_DST;
    }
    if (cfg->node_id > 0xFD && cfg->node_id != UMESH_ADDR_UNASSIGNED) {
        return UMESH_ERR_INVALID_DST;
    }
    if (!cfg->master_key && cfg->security != UMESH_SEC_NONE) {
        return UMESH_ERR_NULL_PTR;
    }
    if (cfg->security != UMESH_SEC_NONE && cfg->security_epoch == 0) {
        return UMESH_ERR_INVALID_DST;
    }
    return UMESH_OK;
}

static void normalize_cfg(umesh_ctx_t *ctx, const umesh_cfg_t *cfg)
{
    ctx->cfg = *cfg;
    if (ctx->cfg.channel == 0) ctx->cfg.channel = UMESH_DEFAULT_CHANNEL;
    if (ctx->cfg.tx_power == 0) ctx->cfg.tx_power = UMESH_TX_POWER_DEFAULT;
    if (ctx->cfg.node_id == 0) ctx->cfg.node_id = UMESH_ADDR_UNASSIGNED;
    if (ctx->cfg.gradient_beacon_ms == 0) {
        ctx->cfg.gradient_beacon_ms = UMESH_GRADIENT_BEACON_MS;
    }
    if (ctx->cfg.gradient_jitter_max_ms == 0) {
        ctx->cfg.gradient_jitter_max_ms = UMESH_GRADIENT_JITTER_MAX_MS;
    }
    if (ctx->cfg.scan_ms == 0) ctx->cfg.scan_ms = UMESH_DISCOVER_TIMEOUT_MS;
    if (ctx->cfg.election_ms == 0) ctx->cfg.election_ms = UMESH_ELECTION_TIMEOUT_MS;
#if UMESH_ENABLE_POWER_MANAGEMENT
    if (ctx->cfg.light_sleep_interval_ms == 0) {
        ctx->cfg.light_sleep_interval_ms = UMESH_LIGHT_SLEEP_INTERVAL_MS;
    }
    if (ctx->cfg.light_listen_window_ms == 0) {
        ctx->cfg.light_listen_window_ms = UMESH_LIGHT_LISTEN_WINDOW_MS;
    }
    if (ctx->cfg.deep_sleep_tx_interval_ms == 0) {
        ctx->cfg.deep_sleep_tx_interval_ms = UMESH_DEEP_SLEEP_TX_INTERVAL_MS;
    }
#else
    ctx->cfg.power_mode = UMESH_POWER_ACTIVE;
    ctx->cfg.light_sleep_interval_ms = 0;
    ctx->cfg.light_listen_window_ms = 0;
    ctx->cfg.deep_sleep_tx_interval_ms = 0;
#endif
}

static umesh_result_t init_bound_ctx(umesh_ctx_t *ctx, const umesh_cfg_t *cfg)
{
    umesh_phy_cfg_t phy_cfg;
    umesh_result_t r;
    uint8_t local_mac[6];
    bool have_local_mac = false;

    if (!ctx) return UMESH_ERR_NULL_PTR;
    r = validate_cfg(cfg);
    if (r != UMESH_OK) {
        memset(ctx, 0, sizeof(*ctx));
        return r;
    }

    memset(ctx, 0, sizeof(*ctx));
    umesh_bind_ctx(ctx);
    normalize_cfg(ctx, cfg);

    phy_cfg.channel  = ctx->cfg.channel;
    phy_cfg.tx_power = ctx->cfg.tx_power;
    phy_cfg.net_id   = ctx->cfg.net_id;
    r = phy_hal_init(&phy_cfg);
    if (r != UMESH_OK) return r;

    r = mac_init(ctx->cfg.node_id);
    if (r != UMESH_OK) return r;

    if (ctx->cfg.master_key) {
        r = sec_init(ctx->cfg.master_key, ctx->cfg.net_id, ctx->cfg.security);
        if (r != UMESH_OK) return r;
    }

    r = net_init(ctx->cfg.net_id, ctx->cfg.node_id, ctx->cfg.role);
    if (r != UMESH_OK) return r;

    if (ctx->cfg.role == UMESH_ROLE_AUTO) {
#ifdef UMESH_PORT_ESP32
        if (esp_wifi_get_mac(WIFI_IF_STA, local_mac) == ESP_OK) {
            have_local_mac = true;
        }
#endif
        if (!have_local_mac) {
            local_mac[0] = 0xAC;
            local_mac[1] = 0x00;
            local_mac[2] = ctx->cfg.net_id;
            local_mac[3] = ctx->cfg.node_id;
            local_mac[4] = ctx->cfg.channel;
            local_mac[5] = 0x01;
            have_local_mac = true;
        }
    }

    net_config_auto(ctx->cfg.scan_ms, ctx->cfg.election_ms,
                    have_local_mac ? local_mac : NULL);
    net_config_routing(ctx->cfg.routing, ctx->cfg.gradient_beacon_ms,
                       ctx->cfg.gradient_jitter_max_ms);
#if UMESH_ENABLE_POWER_MANAGEMENT
    net_config_power(ctx->cfg.power_mode, ctx->cfg.light_sleep_interval_ms,
                     ctx->cfg.light_listen_window_ms);
    power_init(ctx->cfg.power_mode, ctx->cfg.light_sleep_interval_ms,
               ctx->cfg.light_listen_window_ms, ctx->cfg.deep_sleep_tx_interval_ms,
               ctx->cfg.on_sleep, ctx->cfg.on_wake);
#endif

    net_set_rx_callback(on_net_rx);

    ctx->initialized = true;
    return UMESH_OK;
}

umesh_result_t umesh_init_ctx(umesh_ctx_t *ctx, const umesh_cfg_t *cfg)
{
    return init_bound_ctx(ctx_or_default(ctx), cfg);
}

umesh_result_t umesh_start_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_start();
}

umesh_result_t umesh_stop_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_stop();
}

umesh_result_t umesh_reset_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_reset();
}

umesh_result_t umesh_send_ctx(umesh_ctx_t *ctx, uint8_t dst, uint8_t cmd,
                              const void *payload, uint8_t len,
                              uint8_t flags)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_send(dst, cmd, payload, len, flags);
}

umesh_result_t umesh_send_cmd_ctx(umesh_ctx_t *ctx, uint8_t dst, uint8_t cmd,
                                  uint8_t flags)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_send_cmd(dst, cmd, flags);
}

umesh_result_t umesh_broadcast_ctx(umesh_ctx_t *ctx, uint8_t cmd,
                                   const void *payload, uint8_t len)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_broadcast(cmd, payload, len);
}

umesh_result_t umesh_send_raw_ctx(umesh_ctx_t *ctx, uint8_t dst,
                                  const void *payload, uint8_t len,
                                  uint8_t flags)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_send_raw(dst, payload, len, flags);
}

void umesh_on_receive_ctx(umesh_ctx_t *ctx, void (*cb)(umesh_pkt_t *pkt))
{
    umesh_bind_ctx(ctx_or_default(ctx));
    umesh_on_receive(cb);
}

void umesh_on_cmd_ctx(umesh_ctx_t *ctx, uint8_t cmd, void (*cb)(umesh_pkt_t *pkt))
{
    umesh_bind_ctx(ctx_or_default(ctx));
    umesh_on_cmd(cmd, cb);
}

umesh_info_t umesh_get_info_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_info();
}

umesh_role_t umesh_get_role_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_role();
}

bool umesh_is_coordinator_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_is_coordinator();
}

umesh_result_t umesh_trigger_election_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_trigger_election();
}

uint8_t umesh_gradient_distance_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_gradient_distance();
}

umesh_routing_mode_t umesh_get_routing_mode_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_routing_mode();
}

umesh_result_t umesh_gradient_refresh_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_gradient_refresh();
}

uint8_t umesh_get_neighbor_count_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_neighbor_count();
}

umesh_neighbor_t umesh_get_neighbor_ctx(umesh_ctx_t *ctx, uint8_t index)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_neighbor(index);
}

umesh_result_t umesh_set_power_mode_ctx(umesh_ctx_t *ctx, umesh_power_mode_t mode)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_set_power_mode(mode);
}

umesh_power_mode_t umesh_get_power_mode_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_power_mode();
}

umesh_result_t umesh_deep_sleep_cycle_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_deep_sleep_cycle();
}

float umesh_estimate_current_ma_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_estimate_current_ma();
}

umesh_result_t umesh_measure_current_ma_ctx(umesh_ctx_t *ctx, float *out_ma)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_measure_current_ma(out_ma);
}

umesh_power_stats_t umesh_get_power_stats_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_power_stats();
}

umesh_stats_t umesh_get_stats_ctx(umesh_ctx_t *ctx)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    return umesh_get_stats();
}

void umesh_tick_ctx(umesh_ctx_t *ctx, uint32_t now_ms)
{
    umesh_bind_ctx(ctx_or_default(ctx));
    umesh_tick(now_ms);
}

umesh_result_t umesh_init(const umesh_cfg_t *cfg)
{
    return init_bound_ctx(umesh_current_ctx(), cfg);
}

umesh_result_t umesh_start(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
    {
        umesh_result_t r = net_join();
        if (r == UMESH_OK) notify_state_changes();
        return r;
    }
}

umesh_result_t umesh_stop(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
    net_leave();
    return UMESH_OK;
}

umesh_result_t umesh_reset(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    net_leave();
    ctx->initialized = false;
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    ctx->sec.session_epoch = 0;
    ctx->sec.protected_counter = 0;
    memset(ctx->sec.replay, 0, sizeof(ctx->sec.replay));
    memset(ctx->cmd_cb, 0, sizeof(ctx->cmd_cb));
    ctx->rx_cb = NULL;
    ctx->role_notified = false;
    ctx->join_notified = false;
    ctx->gradient_notified = false;
    ctx->last_role = UMESH_ROLE_END_NODE;
    ctx->last_node_id = UMESH_ADDR_UNASSIGNED;
    ctx->last_gradient_distance = UINT8_MAX;
    return UMESH_OK;
}

umesh_result_t umesh_send(uint8_t dst, uint8_t cmd,
                          const void *payload, uint8_t len,
                          uint8_t flags)
{
    umesh_frame_t frame;
    umesh_result_t r;
    umesh_ctx_t *ctx = umesh_current_ctx();

    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
    if (len > UMESH_MAX_PAYLOAD) return UMESH_ERR_TOO_LONG;
    if (!payload && len > 0) return UMESH_ERR_NULL_PTR;

    memset(&frame, 0, sizeof(frame));
    frame.wire_version = UMESH_WIRE_VERSION;
    frame.net_id      = ctx->cfg.net_id;
    frame.dst         = dst;
    frame.src         = net_get_node_id();
    frame.link_src    = frame.src;
    frame.link_dst    = dst;
    frame.flags       = flags;
    frame.cmd         = cmd;
    frame.payload_len = len;
    frame.hop_count   = UMESH_MAX_HOP_COUNT;
    if (len > 0) {
        memcpy(frame.payload, payload, len);
    }

    r = net_route(&frame);
    if (r == UMESH_OK) ctx->stats.tx_count++;
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
    umesh_current_ctx()->rx_cb = cb;
}

void umesh_on_cmd(uint8_t cmd, void (*cb)(umesh_pkt_t *pkt))
{
    umesh_current_ctx()->cmd_cb[cmd] = cb;
}

umesh_info_t umesh_get_info(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    umesh_info_t info;
    info.node_id    = net_get_node_id();
    info.net_id     = ctx->cfg.net_id;
    info.role       = net_get_role();
    info.state      = (umesh_state_t)net_get_state();
    info.coordinator = UMESH_ADDR_COORDINATOR;
    info.node_count = net_get_node_count();
    info.channel    = ctx->cfg.channel;
    info.rssi       = -127;
    info.target     = UMESH_TARGET;
    info.wifi_gen   = (uint8_t)UMESH_WIFI_GEN;
    info.tx_power_max = (uint8_t)UMESH_TX_POWER_MAX;
    return info;
}

const char *umesh_get_target(void)
{
    return UMESH_TARGET;
}

uint8_t umesh_get_wifi_gen(void)
{
    return (uint8_t)UMESH_WIFI_GEN;
}

bool umesh_target_supports(uint32_t capability)
{
    uint32_t mask = 0u;

#if UMESH_HAS_WIFI
    mask |= UMESH_CAP_WIFI;
#endif
#if UMESH_HAS_BT
    mask |= UMESH_CAP_BT;
#endif
#if defined(UMESH_HAS_TWT) && UMESH_HAS_TWT
    mask |= UMESH_CAP_TWT;
#endif
#if UMESH_ENABLE_POWER_MANAGEMENT
    mask |= UMESH_CAP_POWER_MGT;
#endif

    return (mask & capability) == capability;
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
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
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
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
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
#if UMESH_ENABLE_POWER_MANAGEMENT
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
    ctx->cfg.power_mode = mode;
    net_config_power(mode, ctx->cfg.light_sleep_interval_ms,
                     ctx->cfg.light_listen_window_ms);
    return power_set_mode(mode);
#else
    UMESH_UNUSED(mode);
    return UMESH_OK;
#endif
}

umesh_power_mode_t umesh_get_power_mode(void)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    return power_get_mode();
#else
    return UMESH_POWER_ACTIVE;
#endif
}

umesh_result_t umesh_deep_sleep_cycle(void)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return UMESH_ERR_NOT_INIT;
    return power_deep_sleep_cycle(ctx->cfg.routing, net_get_role());
#else
    return UMESH_ERR_NOT_SUPPORTED;
#endif
}

float umesh_estimate_current_ma(void)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    return power_estimate_current_ma();
#else
    return -1.0f;
#endif
}

umesh_result_t umesh_measure_current_ma(float *out_ma)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    UMESH_UNUSED(out_ma);
    return power_measure_current_ma(out_ma);
#else
    UMESH_UNUSED(out_ma);
    return UMESH_ERR_NOT_SUPPORTED;
#endif
}

umesh_power_stats_t umesh_get_power_stats(void)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    return power_get_stats();
#else
    umesh_power_stats_t out;
    out.sleep_count = 0;
    out.total_sleep_ms = 0;
    out.total_active_ms = 0;
    out.duty_cycle_pct = 0.0f;
    out.estimated_ma = 0.0f;
    return out;
#endif
}

umesh_stats_t umesh_get_stats(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    mac_stats_t mac = mac_get_stats();
    ctx->stats.ack_count   = mac.ack_count;
    ctx->stats.retry_count = mac.retry_count;
    ctx->stats.drop_count  = mac.drop_count;
    return ctx->stats;
}

void umesh_tick(uint32_t now_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (!ctx->initialized) return;
    net_tick(now_ms);
#if UMESH_ENABLE_POWER_MANAGEMENT
    power_tick(now_ms, net_get_role());
#endif
    notify_state_changes();
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
    case UMESH_ERR_CRC_FAIL:     return "CRC_FAIL";
    case UMESH_ERR_MIC_FAIL:     return "MIC_FAIL";
    case UMESH_ERR_REPLAY:       return "REPLAY";
    case UMESH_ERR_NOT_SUPPORTED:return "NOT_SUPPORTED";
    default:                     return "UNKNOWN";
    }
}
