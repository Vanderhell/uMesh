# Changelog

All notable changes to µMesh will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [1.0.0] - 2026-03-22

### Added

- **PHY layer** — raw IEEE 802.11 abstraction (`src/phy/`); ESP32 port via
  `esp_wifi_80211_tx` + promiscuous mode (`port/esp32/phy_esp32.c`); POSIX
  loopback simulation for host testing (`port/posix/phy_posix.c`)
- **Common utilities** — CRC16/CCITT (`src/common/crc.c`) and ISR-safe SPSC
  ring buffer (`src/common/ring.c`)
- **MAC layer** — frame serialization/deserialization with CRC validation
  (`src/mac/frame.c`); carrier-sense / CCA (`src/mac/cca.c`); CSMA/CA with
  binary-exponential backoff, ACK mechanism and up to 4 retries
  (`src/mac/mac.c`)
- **Security layer** — AES-128-CTR encryption with per-frame NONCE
  (SRC||NET_ID||SEQ||SALT||COUNTER), HMAC-SHA256 4-byte MIC with
  constant-time comparison, 32-bit sliding-window replay detection
  (`src/sec/sec.c`); AES-128-ECB key derivation for ENC_KEY and AUTH_KEY
  (`src/sec/keys.c`); optional microcrypt back-end
  (`third_party/microcrypt` git submodule)
- **Network layer** — distance-vector routing table with hop×10 + RSSI
  penalty metric, 90 s timeout, 16-entry capacity (`src/net/routing.c`);
  JOIN / ASSIGN / LEAVE / NODE_JOINED / NODE_LEFT discovery protocol
  (`src/net/discovery.c`); network FSM
  (UNINIT→SCANNING→JOINING→CONNECTED→DISCONNECTED), periodic ROUTE_UPDATE
  broadcast (coordinator/router only), SEQ_NUM wrap-around salt regeneration
  to prevent NONCE reuse (`src/net/net.c`)
- **Public API** (`include/umesh.h`, `src/umesh.c`) — `umesh_init`,
  `umesh_start`, `umesh_stop`, `umesh_reset`, `umesh_send`, `umesh_broadcast`,
  `umesh_send_raw`, `umesh_send_cmd`, `umesh_on_receive`, `umesh_on_cmd`,
  `umesh_get_info`, `umesh_get_stats`, `umesh_err_str`
- **Test suite** — 9 test executables (test_crc, test_ring, test_frame,
  test_sec, test_posix_loopback, test_mac, test_routing, test_net, test_e2e),
  all passing on the POSIX build (`tests/`)
- **Examples** — coordinator (`examples/coordinator/main.c`) and end-node
  (`examples/end_node/main.c`) for ESP32 / ESP-IDF

### Fixed

- BUGFIX-02: END_NODE no longer sends ROUTE_UPDATE (coordinator/router only)
- BUGFIX-03: SEQ_NUM wrap-around triggers `sec_regenerate_salt()` to prevent
  NONCE reuse across counter cycles

## [Unreleased]

## [1.4.0] - 2026-03-24

### Added

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

### Changed

- Target-aware CMake source selection for ESP32-C6 TWT stub
- `UMESH_LOW_MEMORY` compile option support

### Verified

- Full POSIX test matrix passes with power management ON and OFF

## [1.3.0] - 2026-03-24

### Added

- Compile-time power feature flag: `UMESH_ENABLE_POWER_MANAGEMENT`
- Runtime power profiles: `ACTIVE`, `LIGHT`, `DEEP`
- Power API (`umesh_set_power_mode`, `umesh_get_power_mode`, `umesh_deep_sleep_cycle`,
  statistics, current estimate)
- Coordinator `POWER_BEACON` command for sleep profile signaling
- ESP32 + POSIX power HAL implementations
- Deep sleep sensor example (`examples/deep_sleep_sensor/`)
- Power documentation (`docs/POWER_MANAGEMENT.md`)

### Changed

- With `-DUMESH_ENABLE_POWER_MANAGEMENT=OFF`, power API remains available via no-op stubs
- Test matrix verified for both PM enabled and disabled

## [1.2.1] - 2026-03-24

### Fixed

- Split-brain convergence in auto coordinator election
- Auto-router transition from election to connected state
- Coordinator route refresh timestamps (prevents intermittent `NOT_ROUTABLE`)
- Dashboard/device serial robustness for late-connect status handshake

### Verified

- 3-node auto-mesh runtime stabilized (1 coordinator + 2 routers)
- 120s hardware capture with active traffic and zero error events

## [1.1.0] - 2026-03-24

### Added

- Auto role mode `UMESH_ROLE_AUTO` with startup coordinator scan window
- Election opcodes:
  - `UMESH_CMD_ELECTION` (`0x57`)
  - `UMESH_CMD_ELECTION_RESULT` (`0x58`)
- Election flow in network FSM (`UMESH_STATE_ELECTION`)
- Public API:
  - `umesh_get_role()`
  - `umesh_is_coordinator()`
  - `umesh_trigger_election()`
- Auto-mesh timing config:
  - `scan_ms`
  - `election_ms`
- New callback: `on_role_elected(umesh_role_t role)`
- Election unit tests (`tests/test_election.c`)
- Single-firmware hardware image for auto mode:
  - `tests/hardware/firmware/auto_mesh_node/auto_mesh_node.ino`

### Changed

- Coordinator failover now re-enters scanning/election when coordinator
  route updates are not seen within `UMESH_NODE_TIMEOUT_MS` in auto mode

## [1.2.0] - 2026-03-24

### Added

- New routing mode enum:
  - `UMESH_ROUTING_DISTANCE_VECTOR` (default)
  - `UMESH_ROUTING_GRADIENT`
- Gradient opcodes:
  - `UMESH_CMD_GRADIENT_BEACON` (`0x59`)
  - `UMESH_CMD_GRADIENT_UPDATE` (`0x5A`)
- Neighbor table for gradient routing (distance + RSSI + expiry)
- Uphill forwarding for coordinator-destined packets in gradient mode
- New public API:
  - `umesh_gradient_distance()`
  - `umesh_get_routing_mode()`
  - `umesh_gradient_refresh()`
  - `umesh_get_neighbor_count()`
  - `umesh_get_neighbor()`
- New config/callback fields:
  - `routing`
  - `gradient_beacon_ms`
  - `gradient_jitter_max_ms`
  - `on_gradient_ready(uint8_t distance)`
- Gradient unit tests (`tests/test_gradient.c`)
- New examples:
  - `examples/gradient_sensor/sensor_node.c`
  - `examples/gradient_sensor/coordinator.c`
- New API documentation:
  - `docs/API_REFERENCE.md`

### Changed

- `README.md` now documents sensor-network gradient mode usage
- `docs/NETWORK_LAYER.md` now includes routing-mode guidance and gradient behavior
