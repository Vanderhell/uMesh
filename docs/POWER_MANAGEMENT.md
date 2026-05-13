# Power management (strict)

This document describes the **API and behavior implemented in code**. It does **not** claim real measured current draw. Any numeric current values are **NOT VERIFIED** unless backed by a linked measurement report.

## Compile-time control

Power management code is compiled behind the `UMESH_ENABLE_POWER_MANAGEMENT` compile definition (see `CMakeLists.txt` and `src/common/defs.h`).

When power management is disabled at compile time:
- `umesh_set_power_mode()` becomes a no-op returning `UMESH_OK`
- `umesh_deep_sleep_cycle()` returns `UMESH_ERR_NOT_SUPPORTED`
- `umesh_estimate_current_ma()` returns a sentinel value (`-1.0f`)

## Modes

The API exposes three modes (`umesh_power_mode_t`):
- `UMESH_POWER_ACTIVE`: radio on (no duty-cycling logic in this module)
- `UMESH_POWER_LIGHT`: periodic duty-cycling (implementation in `src/power/` + port HAL)
- `UMESH_POWER_DEEP`: deep-sleep cycle helper (implementation in `src/power/` + port HAL)

## API surface

- `umesh_result_t umesh_set_power_mode(umesh_power_mode_t mode);`
- `umesh_power_mode_t umesh_get_power_mode(void);`
- `umesh_result_t umesh_deep_sleep_cycle(void);`
- `float umesh_estimate_current_ma(void);` (**NOT VERIFIED** vs real hardware current)
- `umesh_power_stats_t umesh_get_power_stats(void);`

## Configuration fields (defaults)

Defaults are applied in `src/umesh.c` and `src/net/net.c`:
- `light_sleep_interval_ms` defaults to `UMESH_LIGHT_SLEEP_INTERVAL_MS` (currently `1000`)
- `light_listen_window_ms` defaults to `UMESH_LIGHT_LISTEN_WINDOW_MS` (currently `100`)
- `deep_sleep_tx_interval_ms` defaults to `UMESH_DEEP_SLEEP_TX_INTERVAL_MS` (currently `30000`)

## NOT VERIFIED

- Actual current draw in any mode on ESP32-class hardware
- Battery life estimates
