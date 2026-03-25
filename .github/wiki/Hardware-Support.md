# Hardware Support

µMesh support matrix for ESP family:

| Chip | Support | Reason |
|------|---------|--------|
| ESP32 | ✓ Full | Classic, fully tested |
| ESP32-S2 | ✓ Full | No BT, otherwise identical |
| ESP32-S3 | ✓ Full | Recommended, dual-core |
| ESP32-C3 | ✓ Full | Budget option, RISC-V |
| ESP32-C6 | ✓ Full | WiFi 6, best power mgmt |
| ESP32-H2 | ✗ Not supported | No WiFi (802.15.4 only) |
| ESP32-C2 | ✗ Not supported | Insufficient RAM |
| ESP8266 | ✗ Not supported | No raw 802.11 TX API |
| ESP8285 | ✗ Not supported | Same as ESP8266 |

## Why ESP8266 is not supported

ESP8266 does not provide `esp_wifi_80211_tx()`, which µMesh requires to send raw 802.11 frames. Promiscuous RX alone is not enough for active mesh participation.

## Why ESP32-H2 is not supported

ESP32-H2 is an 802.15.4 (Zigbee/Thread) target, not a WiFi target. µMesh depends on WiFi PHY capability.

## Why ESP32-C2 is not supported

ESP32-C2 RAM is too constrained for the full µMesh stack and expected routing/security workloads.
