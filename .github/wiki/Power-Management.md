# Power Management

µMesh provides runtime power profiles via `umesh_set_power_mode(...)`.

## Modes

## ACTIVE

- Radio stays fully available
- Lowest latency
- Highest current draw
- Best for coordinators and latency-sensitive routers

## LIGHT

- Periodic sleep with short listen windows
- Balanced power vs responsiveness
- Tuned by:
  - `light_sleep_interval_ms`
  - `light_listen_window_ms`

## DEEP

- Aggressive sleep intervals with periodic wake/send cycles
- Lowest average power
- Highest latency
- Tuned by:
  - `deep_sleep_tx_interval_ms`

## API

- `umesh_set_power_mode(UMESH_POWER_ACTIVE | UMESH_POWER_LIGHT | UMESH_POWER_DEEP)`
- `umesh_get_power_mode()`
- `umesh_deep_sleep_cycle()`
- `umesh_estimate_current_ma()`
- `umesh_get_power_stats()`

## Practical guidance

- Coordinator: keep `ACTIVE`.
- Routers: use `ACTIVE` or carefully tuned `LIGHT`.
- End nodes: `LIGHT` or `DEEP` depending on reporting interval.
- Validate packet delivery and retries after changing power profile.
