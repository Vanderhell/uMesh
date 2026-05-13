# API Reference (strict)

This document describes the public API declared in `include/umesh.h`.

Verification policy:
- “Default value” claims below are derived from `src/umesh.c` (argument sanitization + fallback defaults) and constants in `src/common/defs.h`.
- Anything that depends on ESP-IDF, hardware radios, or power measurements is **NOT VERIFIED** here.

## Core types

Public configuration struct:
- `umesh_cfg_t` (see `include/umesh.h`)

Important enums / constants used by the API:
- roles: `umesh_role_t`
- security level: `umesh_security_t`
- routing mode: `umesh_routing_mode_t`
- power mode: `umesh_power_mode_t`
- result codes: `umesh_result_t`

These are currently defined via internal headers in the repository; see `src/common/defs.h`. (This is an include-hygiene issue tracked by the audit; see `VERIFICATION.md` and `README.md` for status.)

## Initialization and lifecycle

- `umesh_result_t umesh_init(const umesh_cfg_t *cfg);`
- `umesh_result_t umesh_start(void);`
- `umesh_result_t umesh_stop(void);`
- `umesh_result_t umesh_reset(void);`

### Config fields and defaults (from `src/umesh.c`)

Required (caller responsibility):
- `net_id` (no default; used for filtering/identity in multiple layers)

Common optional fields (defaults applied if zero/invalid):
- `node_id`
  - if `0` → treated as `UMESH_ADDR_UNASSIGNED`
- `channel`
  - if `0` → `UMESH_DEFAULT_CHANNEL` (currently `6`)
- `tx_power`
  - if `0` → `UMESH_TX_POWER_DEFAULT` (currently `60`)
- `security`
  - invalid enum values are clamped to `UMESH_SEC_FULL`
  - NOTE: security requires `master_key != NULL` to initialize crypto in `umesh_init()`
- `role`
  - invalid enum values are clamped to `UMESH_ROLE_AUTO`
- `routing`
  - invalid enum values are clamped to `UMESH_ROUTING_DISTANCE_VECTOR`

Gradient fields:
- `gradient_beacon_ms`
  - if `0` → `UMESH_GRADIENT_BEACON_MS` (currently `30000`)
- `gradient_jitter_max_ms`
  - if `0` → `UMESH_GRADIENT_JITTER_MAX_MS` (currently `200`)
- `on_gradient_ready(uint8_t distance)`
  - called when a non-`UINT8_MAX` gradient distance becomes available (see `src/umesh.c`)

Auto-mesh fields:
- `scan_ms` (default is applied in `src/net/net.c` when configuring auto timing)
- `election_ms` (default is applied in `src/net/net.c` when configuring auto timing)
- `on_role_elected(umesh_role_t role)`

Power-management fields (only meaningful when compiled with power management enabled):
- `power_mode`
  - default `UMESH_POWER_ACTIVE`
- `light_sleep_interval_ms`
  - if `0` → `UMESH_LIGHT_SLEEP_INTERVAL_MS` (currently `1000`)
- `light_listen_window_ms`
  - if `0` → `UMESH_LIGHT_LISTEN_WINDOW_MS` (currently `100`)
- `deep_sleep_tx_interval_ms`
  - if `0` → `UMESH_DEEP_SLEEP_TX_INTERVAL_MS` (currently `30000`)
- `on_sleep(void)`, `on_wake(void)`

## Send APIs

- `umesh_result_t umesh_send(uint8_t dst, uint8_t cmd, const void *payload, uint8_t len, uint8_t flags);`
- `umesh_result_t umesh_send_cmd(uint8_t dst, uint8_t cmd, uint8_t flags);`
- `umesh_result_t umesh_broadcast(uint8_t cmd, const void *payload, uint8_t len);`
- `umesh_result_t umesh_send_raw(uint8_t dst, const void *payload, uint8_t len, uint8_t flags);`

Send-path behavior (code-visible in `src/umesh.c` + `src/mac/mac.c`):
- Payload length is limited to `UMESH_MAX_PAYLOAD` (currently `64`).
- If security was initialized via `umesh_init()` (`cfg.security != UMESH_SEC_NONE` and `cfg.master_key != NULL`), `mac_send()` enforces `sec_encrypt_frame()` for non-ACK frames before serialization.
- Packets are routed via `net_route()`, which eventually calls `mac_send()`.

## Receive callbacks

- `void umesh_on_receive(void (*cb)(umesh_pkt_t *pkt));`
- `void umesh_on_cmd(uint8_t cmd, void (*cb)(umesh_pkt_t *pkt));`

Callback dispatch is implemented in `src/umesh.c`:
- per-command handler (if registered) runs first
- then the generic receive handler (if registered)

## Introspection APIs

- `umesh_info_t  umesh_get_info(void);`
- `umesh_stats_t umesh_get_stats(void);`
- `const char   *umesh_err_str(umesh_result_t err);`

Target/capability helpers:
- `const char *umesh_get_target(void);` (from `include/umesh_caps.h`)
- `uint8_t     umesh_get_wifi_gen(void);` (from `include/umesh_caps.h`)
- `bool        umesh_target_supports(uint32_t capability);`

## Routing helpers

- `uint8_t umesh_gradient_distance(void);`
- `umesh_routing_mode_t umesh_get_routing_mode(void);`
- `umesh_result_t umesh_gradient_refresh(void);`
- `uint8_t umesh_get_neighbor_count(void);`
- `umesh_neighbor_t umesh_get_neighbor(uint8_t index);`

## Power-management helpers

- `umesh_result_t umesh_set_power_mode(umesh_power_mode_t mode);`
- `umesh_power_mode_t umesh_get_power_mode(void);`
- `umesh_result_t umesh_deep_sleep_cycle(void);`
- `float umesh_estimate_current_ma(void);` (**NOT VERIFIED** against real hardware current)
- `umesh_power_stats_t umesh_get_power_stats(void);`
