#include "power.h"
#if UMESH_ENABLE_POWER_MANAGEMENT
#include "power_hal.h"

static umesh_power_mode_t s_mode = UMESH_POWER_ACTIVE;
static uint32_t s_light_interval_ms = UMESH_LIGHT_SLEEP_INTERVAL_MS;
static uint32_t s_light_window_ms = UMESH_LIGHT_LISTEN_WINDOW_MS;
static uint32_t s_deep_tx_interval_ms = UMESH_DEEP_SLEEP_TX_INTERVAL_MS;
static uint32_t s_last_tick_ms = 0;
static bool s_initialized = false;
static bool s_in_sleep_phase = false;
static void (*s_on_sleep)(void) = 0;
static void (*s_on_wake)(void) = 0;
static umesh_power_stats_t s_stats;

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static float estimate_mode_ma(umesh_power_mode_t mode)
{
    if (mode == UMESH_POWER_ACTIVE) {
        return 60.0f;
    }
    if (mode == UMESH_POWER_LIGHT) {
        uint32_t interval = (s_light_interval_ms == 0)
            ? UMESH_LIGHT_SLEEP_INTERVAL_MS : s_light_interval_ms;
        uint32_t active = (s_light_window_ms > interval) ? interval : s_light_window_ms;
        uint32_t sleep = interval - active;
        return (((float)active * 60.0f) + ((float)sleep * 2.0f)) / (float)interval;
    }
    {
        float active_ms = 500.0f;
        float sleep_ms = (float)((s_deep_tx_interval_ms > 500)
            ? (s_deep_tx_interval_ms - 500) : 0);
        float total = active_ms + sleep_ms;
        if (total <= 0.0f) return 80.0f;
        return ((active_ms * 80.0f) + (sleep_ms * 0.01f)) / total;
    }
}

static void update_stats_window(uint32_t now_ms, umesh_role_t role)
{
    uint32_t delta;
    if (!s_initialized) return;
    if (now_ms <= s_last_tick_ms) {
        s_last_tick_ms = now_ms;
        return;
    }

    delta = now_ms - s_last_tick_ms;
    s_last_tick_ms = now_ms;

    if (s_mode == UMESH_POWER_ACTIVE || role != UMESH_ROLE_END_NODE) {
        s_stats.total_active_ms += delta;
        s_in_sleep_phase = false;
        return;
    }

    if (s_mode == UMESH_POWER_LIGHT) {
        uint32_t interval = (s_light_interval_ms == 0)
            ? UMESH_LIGHT_SLEEP_INTERVAL_MS : s_light_interval_ms;
        uint32_t window = (s_light_window_ms > interval) ? interval : s_light_window_ms;
        uint32_t remain = delta;
        uint32_t t = (now_ms - delta) % interval;

        while (remain > 0) {
            uint32_t chunk = min_u32(remain, interval - t);
            bool active = (t < window);
            if (active) {
                s_stats.total_active_ms += chunk;
                if (s_in_sleep_phase) {
                    s_in_sleep_phase = false;
                    if (s_on_wake) s_on_wake();
                }
            } else {
                s_stats.total_sleep_ms += chunk;
                if (!s_in_sleep_phase) {
                    s_in_sleep_phase = true;
                    s_stats.sleep_count++;
                    if (s_on_sleep) s_on_sleep();
                }
            }
            remain -= chunk;
            t = 0;
        }
        return;
    }

    /* Deep mode: at runtime we still track active time in the normal loop.
     * Actual sleeping is handled in power_deep_sleep_cycle(). */
    s_stats.total_active_ms += delta;
}

umesh_result_t power_init(umesh_power_mode_t mode,
                          uint32_t light_interval_ms,
                          uint32_t light_window_ms,
                          uint32_t deep_sleep_tx_interval_ms,
                          void (*on_sleep)(void),
                          void (*on_wake)(void))
{
    s_mode = mode;
    s_light_interval_ms = (light_interval_ms == 0)
        ? UMESH_LIGHT_SLEEP_INTERVAL_MS : light_interval_ms;
    s_light_window_ms = (light_window_ms == 0)
        ? UMESH_LIGHT_LISTEN_WINDOW_MS : light_window_ms;
    s_deep_tx_interval_ms = (deep_sleep_tx_interval_ms == 0)
        ? UMESH_DEEP_SLEEP_TX_INTERVAL_MS : deep_sleep_tx_interval_ms;
    if (s_light_window_ms > s_light_interval_ms) {
        s_light_window_ms = s_light_interval_ms;
    }
    s_on_sleep = on_sleep;
    s_on_wake = on_wake;
    s_last_tick_ms = 0;
    s_initialized = true;
    s_in_sleep_phase = false;
    s_stats.sleep_count = 0;
    s_stats.total_sleep_ms = 0;
    s_stats.total_active_ms = 0;
    s_stats.duty_cycle_pct = 100.0f;
    s_stats.estimated_ma = estimate_mode_ma(s_mode);
    return UMESH_OK;
}

void power_tick(uint32_t now_ms, umesh_role_t role)
{
    uint32_t total_ms;
    update_stats_window(now_ms, role);
    total_ms = s_stats.total_active_ms + s_stats.total_sleep_ms;
    if (total_ms > 0) {
        s_stats.duty_cycle_pct = ((float)s_stats.total_active_ms * 100.0f) /
                                 (float)total_ms;
    } else {
        s_stats.duty_cycle_pct = 100.0f;
    }
    s_stats.estimated_ma = estimate_mode_ma(s_mode);
}

umesh_result_t power_set_mode(umesh_power_mode_t mode)
{
    s_mode = mode;
    s_in_sleep_phase = false;
    s_stats.estimated_ma = estimate_mode_ma(s_mode);
    return UMESH_OK;
}

umesh_power_mode_t power_get_mode(void)
{
    return s_mode;
}

void power_set_light_profile(uint32_t interval_ms, uint32_t window_ms)
{
    if (interval_ms > 0) s_light_interval_ms = interval_ms;
    if (window_ms > 0) s_light_window_ms = window_ms;
    if (s_light_window_ms > s_light_interval_ms) {
        s_light_window_ms = s_light_interval_ms;
    }
    s_stats.estimated_ma = estimate_mode_ma(s_mode);
}

void power_set_deep_interval(uint32_t interval_ms)
{
    if (interval_ms > 0) s_deep_tx_interval_ms = interval_ms;
    s_stats.estimated_ma = estimate_mode_ma(s_mode);
}

umesh_result_t power_deep_sleep_cycle(umesh_routing_mode_t routing_mode,
                                      umesh_role_t role)
{
    if (s_mode != UMESH_POWER_DEEP) return UMESH_ERR_INVALID_DST;
    if (role == UMESH_ROLE_ROUTER || role == UMESH_ROLE_COORDINATOR) {
        return UMESH_ERR_INVALID_DST;
    }
    if (routing_mode != UMESH_ROUTING_GRADIENT) {
        return UMESH_ERR_NOT_ROUTABLE;
    }

    s_stats.sleep_count++;
    s_stats.total_sleep_ms += s_deep_tx_interval_ms;
    if (s_on_sleep) s_on_sleep();
    power_hal_deep_sleep(s_deep_tx_interval_ms);
    if (s_on_wake) s_on_wake();
    return UMESH_OK;
}

float power_estimate_current_ma(void)
{
    return estimate_mode_ma(s_mode);
}

umesh_power_stats_t power_get_stats(void)
{
    return s_stats;
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
