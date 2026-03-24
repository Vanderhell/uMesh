# µMesh v1.4.0

Broader ESP target support release with capability detection and C6 TWT groundwork.

## Added
- New compile-time target capability header: `include/umesh_caps.h`
- ESP32 target matrix for ESP32 / S2 / S3 / C3 / C6
- Unsupported target guards for ESP32-H2 and ESP32-C2
- Runtime capability API:
  - `umesh_get_target()`
  - `umesh_get_wifi_gen()`
  - `umesh_target_supports(...)`
- Extended `umesh_info_t` with target metadata (`target`, `wifi_gen`, `tx_power_max`)
- ESP32-C6 TWT stub (`port/esp32/twt_esp32c6.c`)
- New capability tests (`tests/test_caps.c`)
- New target info example (`examples/target_info/`)

## Build/Config
- Target-aware CMake source selection for ESP32-C6 TWT stub
- `UMESH_LOW_MEMORY` compile option support
- Version bump to `v1.4.0`

## Verification
- Full POSIX test matrix passes with power management ON and OFF.

## Included Assets
- `umesh-v1.4.0-source.zip`
- `umesh-v1.4.0-hardware-firmware.zip`
- `umesh-v1.4.0-target-info-example.zip`
- `SHA256SUMS.txt`