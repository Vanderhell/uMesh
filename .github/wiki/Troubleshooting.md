# Troubleshooting

Common issues and quick fixes.

## Node not joining

Symptoms:

- Node remains unassigned or disconnected

Checks:

- `net_id` must match on all nodes
- `master_key` must match exactly on all nodes
- `channel` must match
- Ensure role setup is valid (`UMESH_ROLE_AUTO` is easiest)

## `NOT_ROUTABLE` error

Symptoms:

- `umesh_send(...)` fails with `NOT_ROUTABLE`

Checks:

- In gradient mode, wait until gradient is established (`on_gradient_ready`)
- Verify coordinator is online and sending beacons
- Reduce distance or obstructions while converging

## High retry count

Symptoms:

- Elevated `retry_count`, delayed delivery

Checks:

- Reduce node spacing
- Remove obstacles/metal shielding
- Prefer channels 1, 6, or 11
- Lower congestion from nearby AP traffic

## USB CDC not working on ESP32-C3

Symptoms:

- No serial output in test runner

Fix:

- In Arduino IDE, set `USB CDC On Boot` to `Enabled`
- Reflash and reconnect serial monitor

## Tests failing at distance

Symptoms:

- Intermittent packet loss in hardware test runner

Checks:

- Mini modules often have weaker antennas
- Start with shorter spacing, then increase gradually
- Keep antennas vertical and clear of cables/enclosures
- Increase relay density (more routers) for long paths
