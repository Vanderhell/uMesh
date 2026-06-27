#include "power.h"
#include "../context.h"
#if UMESH_ENABLE_POWER_MANAGEMENT
#include "power_hal.h"

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static float estimate_mode_ma(umesh_ctx_t *ctx, umesh_power_mode_t mode)
{
    if (mode == UMESH_POWER_ACTIVE) {
        return 60.0f;
    }
    if (mode == UMESH_POWER_LIGHT) {
        uint32_t interval = (ctx->power.light_interval_ms == 0)
            ? UMESH_LIGHT_SLEEP_INTERVAL_MS : ctx->power.light_interval_ms;
        uint32_t active = (ctx->power.light_window_ms > interval) ? interval : ctx->power.light_window_ms;
        uint32_t sleep = interval - active;
        return (((float)active * 60.0f) + ((float)sleep * 2.0f)) / (float)interval;
    }
    {
        float active_ms = 500.0f;
        float sleep_ms = (float)((ctx->power.deep_tx_interval_ms > 500)
            ? (ctx->power.deep_tx_interval_ms - 500) : 0);
        float total = active_ms + sleep_ms;
        if (total <= 0.0f) return 80.0f;
        return ((active_ms * 80.0f) + (sleep_ms * 0.01f)) / total;
    }
}

static void update_stats_window(umesh_ctx_t *ctx, uint32_t now_ms, umesh_role_t role)
{
    uint32_t delta;
    if (!ctx->power.initialized) return;
    if (now_ms <= ctx->power.last_tick_ms) {
        ctx->power.last_tick_ms = now_ms;
        return;
    }

    delta = now_ms - ctx->power.last_tick_ms;
    ctx->power.last_tick_ms = now_ms;

    if (ctx->power.mode == UMESH_POWER_ACTIVE || role != UMESH_ROLE_END_NODE) {
        ctx->power.stats.total_active_ms += delta;
        ctx->power.in_sleep_phase = false;
        return;
    }

    if (ctx->power.mode == UMESH_POWER_LIGHT) {
        uint32_t interval = (ctx->power.light_interval_ms == 0)
            ? UMESH_LIGHT_SLEEP_INTERVAL_MS : ctx->power.light_interval_ms;
        uint32_t window = (ctx->power.light_window_ms > interval) ? interval : ctx->power.light_window_ms;
        uint32_t remain = delta;
        uint32_t t = (now_ms - delta) % interval;

        while (remain > 0) {
            uint32_t chunk = min_u32(remain, interval - t);
            bool active = (t < window);
            if (active) {
                ctx->power.stats.total_active_ms += chunk;
                if (ctx->power.in_sleep_phase) {
                    ctx->power.in_sleep_phase = false;
                    if (ctx->power.on_wake) ctx->power.on_wake();
                }
            } else {
                ctx->power.stats.total_sleep_ms += chunk;
                if (!ctx->power.in_sleep_phase) {
                    ctx->power.in_sleep_phase = true;
                    ctx->power.stats.sleep_count++;
                    if (ctx->power.on_sleep) ctx->power.on_sleep();
                }
            }
            remain -= chunk;
            t = 0;
        }
        return;
    }

    ctx->power.stats.total_active_ms += delta;
}

umesh_result_t power_init(umesh_power_mode_t mode,
                          uint32_t light_interval_ms,
                          uint32_t light_window_ms,
                          uint32_t deep_sleep_tx_interval_ms,
                          void (*on_sleep)(void),
                          void (*on_wake)(void))
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->power.mode = mode;
    ctx->power.light_interval_ms = (light_interval_ms == 0)
        ? UMESH_LIGHT_SLEEP_INTERVAL_MS : light_interval_ms;
    ctx->power.light_window_ms = (light_window_ms == 0)
        ? UMESH_LIGHT_LISTEN_WINDOW_MS : light_window_ms;
    ctx->power.deep_tx_interval_ms = (deep_sleep_tx_interval_ms == 0)
        ? UMESH_DEEP_SLEEP_TX_INTERVAL_MS : deep_sleep_tx_interval_ms;
    if (ctx->power.light_window_ms > ctx->power.light_interval_ms) {
        ctx->power.light_window_ms = ctx->power.light_interval_ms;
    }
    ctx->power.on_sleep = on_sleep;
    ctx->power.on_wake = on_wake;
    ctx->power.last_tick_ms = 0;
    ctx->power.initialized = true;
    ctx->power.in_sleep_phase = false;
    ctx->power.stats.sleep_count = 0;
    ctx->power.stats.total_sleep_ms = 0;
    ctx->power.stats.total_active_ms = 0;
    ctx->power.stats.duty_cycle_pct = 100.0f;
    ctx->power.stats.estimated_ma = estimate_mode_ma(ctx, ctx->power.mode);
    return UMESH_OK;
}

void power_tick(uint32_t now_ms, umesh_role_t role)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint32_t total_ms;
    update_stats_window(ctx, now_ms, role);
    total_ms = ctx->power.stats.total_active_ms + ctx->power.stats.total_sleep_ms;
    if (total_ms > 0) {
        ctx->power.stats.duty_cycle_pct = ((float)ctx->power.stats.total_active_ms * 100.0f) /
                                 (float)total_ms;
    } else {
        ctx->power.stats.duty_cycle_pct = 100.0f;
    }
    ctx->power.stats.estimated_ma = estimate_mode_ma(ctx, ctx->power.mode);
}

umesh_result_t power_set_mode(umesh_power_mode_t mode)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->power.mode = mode;
    ctx->power.in_sleep_phase = false;
    ctx->power.stats.estimated_ma = estimate_mode_ma(ctx, ctx->power.mode);
    return UMESH_OK;
}

umesh_power_mode_t power_get_mode(void)
{
    return umesh_current_ctx()->power.mode;
}

void power_set_light_profile(uint32_t interval_ms, uint32_t window_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (interval_ms > 0) ctx->power.light_interval_ms = interval_ms;
    if (window_ms > 0) ctx->power.light_window_ms = window_ms;
    if (ctx->power.light_window_ms > ctx->power.light_interval_ms) {
        ctx->power.light_window_ms = ctx->power.light_interval_ms;
    }
    ctx->power.stats.estimated_ma = estimate_mode_ma(ctx, ctx->power.mode);
}

void power_set_deep_interval(uint32_t interval_ms)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (interval_ms > 0) ctx->power.deep_tx_interval_ms = interval_ms;
    ctx->power.stats.estimated_ma = estimate_mode_ma(ctx, ctx->power.mode);
}

umesh_result_t power_deep_sleep_cycle(umesh_routing_mode_t routing_mode,
                                      umesh_role_t role)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    if (ctx->power.mode != UMESH_POWER_DEEP) return UMESH_ERR_INVALID_DST;
    if (role == UMESH_ROLE_ROUTER || role == UMESH_ROLE_COORDINATOR) {
        return UMESH_ERR_INVALID_DST;
    }
    if (routing_mode != UMESH_ROUTING_GRADIENT) {
        return UMESH_ERR_NOT_ROUTABLE;
    }

    ctx->power.stats.sleep_count++;
    ctx->power.stats.total_sleep_ms += ctx->power.deep_tx_interval_ms;
    if (ctx->power.on_sleep) ctx->power.on_sleep();
    power_hal_deep_sleep(ctx->power.deep_tx_interval_ms);
    return UMESH_OK;
}

float power_estimate_current_ma(void)
{
    return estimate_mode_ma(umesh_current_ctx(), umesh_current_ctx()->power.mode);
}

umesh_result_t power_measure_current_ma(float *out_ma)
{
    UMESH_UNUSED(out_ma);
    return UMESH_ERR_NOT_SUPPORTED;
}

umesh_power_stats_t power_get_stats(void)
{
    return umesh_current_ctx()->power.stats;
}
#else
umesh_result_t power_init(umesh_power_mode_t mode,
                          uint32_t light_interval_ms,
                          uint32_t light_window_ms,
                          uint32_t deep_sleep_tx_interval_ms,
                          void (*on_sleep)(void),
                          void (*on_wake)(void))
{
    UMESH_UNUSED(mode);
    UMESH_UNUSED(light_interval_ms);
    UMESH_UNUSED(light_window_ms);
    UMESH_UNUSED(deep_sleep_tx_interval_ms);
    UMESH_UNUSED(on_sleep);
    UMESH_UNUSED(on_wake);
    return UMESH_OK;
}

void power_tick(uint32_t now_ms, umesh_role_t role)
{
    UMESH_UNUSED(now_ms);
    UMESH_UNUSED(role);
}

umesh_result_t power_set_mode(umesh_power_mode_t mode)
{
    UMESH_UNUSED(mode);
    return UMESH_OK;
}

umesh_power_mode_t power_get_mode(void)
{
    return UMESH_POWER_ACTIVE;
}

void power_set_light_profile(uint32_t interval_ms, uint32_t window_ms)
{
    UMESH_UNUSED(interval_ms);
    UMESH_UNUSED(window_ms);
}

void power_set_deep_interval(uint32_t interval_ms)
{
    UMESH_UNUSED(interval_ms);
}

umesh_result_t power_deep_sleep_cycle(umesh_routing_mode_t routing_mode,
                                      umesh_role_t role)
{
    UMESH_UNUSED(routing_mode);
    UMESH_UNUSED(role);
    return UMESH_ERR_NOT_SUPPORTED;
}

float power_estimate_current_ma(void)
{
    return -1.0f;
}

umesh_result_t power_measure_current_ma(float *out_ma)
{
    UMESH_UNUSED(out_ma);
    return UMESH_ERR_NOT_SUPPORTED;
}

umesh_power_stats_t power_get_stats(void)
{
    umesh_power_stats_t out;
    out.sleep_count = 0;
    out.total_sleep_ms = 0;
    out.total_active_ms = 0;
    out.duty_cycle_pct = 0.0f;
    out.estimated_ma = 0.0f;
    return out;
}
#endif
