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
