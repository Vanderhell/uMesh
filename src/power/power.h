#ifndef UMESH_POWER_H
#define UMESH_POWER_H

#include "../common/defs.h"

umesh_result_t power_init(umesh_power_mode_t mode,
                          uint32_t light_interval_ms,
                          uint32_t light_window_ms,
                          uint32_t deep_sleep_tx_interval_ms,
                          void (*on_sleep)(void),
                          void (*on_wake)(void));
void           power_tick(uint32_t now_ms, umesh_role_t role);
umesh_result_t power_set_mode(umesh_power_mode_t mode);
umesh_power_mode_t power_get_mode(void);
umesh_result_t power_deep_sleep_cycle(umesh_routing_mode_t routing_mode,
                                      umesh_role_t role);
float          power_estimate_current_ma(void);
umesh_power_stats_t power_get_stats(void);
void           power_set_light_profile(uint32_t interval_ms, uint32_t window_ms);
void           power_set_deep_interval(uint32_t interval_ms);

#endif /* UMESH_POWER_H */
