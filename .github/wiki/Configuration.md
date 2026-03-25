# Configuration

`umesh_cfg_t` controls network identity, role behavior, routing, power profile, and callbacks.

## Core fields

| Field | Type | Meaning | Typical value / default |
|---|---|---|---|
| `net_id` | `uint8_t` | Logical network ID. Nodes join only matching networks. | `0x01` |
| `node_id` | `uint8_t` | Node address. Use unassigned for auto assignment. | `UMESH_ADDR_UNASSIGNED (0xFE)` |
| `master_key` | `const uint8_t*` | 16-byte key for AUTH/FULL security. | Required for secure mesh |
| `role` | `umesh_role_t` | Startup role mode. | `UMESH_ROLE_AUTO` recommended |
| `security` | `umesh_security_t` | Security level (`NONE`, `AUTH`, `FULL`). | `UMESH_SEC_FULL` |
| `channel` | `uint8_t` | WiFi channel used for raw frames. | default `6` when `0` |
| `tx_power` | `uint8_t` | PHY TX power setting. | default `60` when `0` |

## Routing and auto-mesh fields

| Field | Type | Meaning | Default when `0` |
|---|---|---|---|
| `routing` | `umesh_routing_mode_t` | `DISTANCE_VECTOR` or `GRADIENT`. | `DISTANCE_VECTOR` |
| `scan_ms` | `uint32_t` | Auto-role scan window before election. | `2000` |
| `election_ms` | `uint32_t` | Auto-role election timeout. | `1000` |
| `gradient_beacon_ms` | `uint32_t` | Coordinator gradient beacon period. | `30000` |
| `gradient_jitter_max_ms` | `uint32_t` | Randomized beacon jitter cap. | `200` |

## Power fields

| Field | Type | Meaning | Default when `0` |
|---|---|---|---|
| `power_mode` | `umesh_power_mode_t` | `ACTIVE`, `LIGHT`, or `DEEP`. | `ACTIVE` |
| `light_sleep_interval_ms` | `uint32_t` | Sleep interval for LIGHT mode. | `1000` |
| `light_listen_window_ms` | `uint32_t` | RX listen window for LIGHT mode. | `100` |
| `deep_sleep_tx_interval_ms` | `uint32_t` | Wake/send interval in DEEP mode. | `30000` |

## Callback fields

| Field | Signature | Trigger |
|---|---|---|
| `on_joined` | `void (*)(uint8_t node_id)` | Node gets valid ID and joins network |
| `on_role_elected` | `void (*)(umesh_role_t role)` | Auto election chooses role |
| `on_node_joined` | `void (*)(uint8_t node_id)` | Peer joined notification |
| `on_node_left` | `void (*)(uint8_t node_id)` | Peer timeout/leave notification |
| `on_gradient_ready` | `void (*)(uint8_t distance)` | First valid gradient distance available |
| `on_sleep` | `void (*)(void)` | Power subsystem enters sleep |
| `on_wake` | `void (*)(void)` | Power subsystem wakes |
| `on_error` | `void (*)(umesh_result_t err)` | Internal error callback |

## Minimal recommended config

```c
umesh_cfg_t cfg = {
    .net_id = 0x01,
    .node_id = UMESH_ADDR_UNASSIGNED,
    .master_key = KEY,
    .role = UMESH_ROLE_AUTO,
    .security = UMESH_SEC_FULL,
    .channel = 6,
    .routing = UMESH_ROUTING_GRADIENT,
};
```
