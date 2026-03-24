# µMesh v1.3.0

Power management release focused on lowering sensor-node idle consumption.

## Added
- Compile-time power feature flag: `UMESH_ENABLE_POWER_MANAGEMENT`
- Runtime power profiles: `ACTIVE`, `LIGHT`, `DEEP`
- Power API (`umesh_set_power_mode`, `umesh_get_power_mode`, `umesh_deep_sleep_cycle`, statistics, current estimate)
- Coordinator `POWER_BEACON` command for sleep profile signaling
- ESP32 + POSIX power HAL implementations
- Deep sleep sensor example (`examples/deep_sleep_sensor/`)
- Power documentation (`docs/POWER_MANAGEMENT.md`)

## Compatibility
- With `-DUMESH_ENABLE_POWER_MANAGEMENT=OFF`, power API remains available via no-op stubs.
- Test matrix verified for both PM enabled and disabled.

## Included Assets
- `umesh-v1.3.0-source.zip`
- `umesh-v1.3.0-hardware-firmware.zip`
- `umesh-v1.3.0-deep-sleep-example.zip`
- `SHA256SUMS.txt`