# uMesh API Reference

## umesh_cfg_t

Core fields:
- `net_id`
- `node_id`
- `master_key`
- `role`
- `security`
- `channel`
- `tx_power`

Auto-mesh fields:
- `scan_ms` (default 2000)
- `election_ms` (default 1000)
- `on_role_elected(umesh_role_t role)`

Gradient routing fields:
- `routing` (`UMESH_ROUTING_DISTANCE_VECTOR` or `UMESH_ROUTING_GRADIENT`)
- `gradient_beacon_ms` (default 30000)
- `gradient_jitter_max_ms` (default 200)
- `on_gradient_ready(uint8_t distance)`

## Role and election API

- `umesh_get_role(void)`
- `umesh_is_coordinator(void)`
- `umesh_trigger_election(void)`

## Gradient routing API

- `uint8_t umesh_gradient_distance(void)`
  - returns hop distance to coordinator
  - `UINT8_MAX` means not established yet

- `umesh_routing_mode_t umesh_get_routing_mode(void)`

- `umesh_result_t umesh_gradient_refresh(void)`
  - coordinator-only
  - forces immediate gradient beacon

- `uint8_t umesh_get_neighbor_count(void)`

- `umesh_neighbor_t umesh_get_neighbor(uint8_t index)`
  - returns neighbor diagnostics (`node_id`, `distance`, `rssi`, `last_seen_ms`)

## Example: sensor node in gradient mode

```c
umesh_cfg_t cfg = {
    .net_id = 0x01,
    .node_id = UMESH_ADDR_UNASSIGNED,
    .master_key = KEY,
    .role = UMESH_ROLE_AUTO,
    .security = UMESH_SEC_FULL,
    .channel = 6,
    .routing = UMESH_ROUTING_GRADIENT,
    .on_gradient_ready = on_ready,
};
```
