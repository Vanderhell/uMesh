# µMesh

> **Lightweight open mesh protocol over raw 802.11 for ESP32.**
> No router. No infrastructure. No proprietary dependencies.

[![Language: C99](https://img.shields.io/badge/language-C99-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](#)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3%20%7C%20ESP32--C3-lightgrey.svg)](#)
[![Status: v1.3.0](https://img.shields.io/badge/status-v1.3.0-brightgreen.svg)](#)

---

## What is µMesh?

µMesh is a complete **network protocol stack** built on top of raw IEEE 802.11 frames on ESP32. It uses the WiFi hardware — antenna, radio chip — but **bypasses the entire WiFi protocol stack.** No router, no association, no infrastructure.

Result: peer-to-peer and mesh communication between ESP32 devices anywhere, instantly, without any network configuration.

---

## Why not ESP-NOW?

ESP-NOW is a great idea — but it has fundamental limitations:

| | ESP-NOW | **µMesh** |
|---|---|---|
| Open protocol | ✗ Proprietary | **✓ MIT license** |
| Mesh routing | ✗ 1 hop only | **✓ Multi-hop** |
| Addressing | ✗ MAC only | **✓ NET_ID + NODE_ID** |
| Discovery | ✗ Manual | **✓ Automatic** |
| Security | ~ Basic | **✓ AES-128 + HMAC** |
| Command layer | ✗ None | **✓ Opcode table** |
| Portability | ✗ Espressif only | **✓ Abstracted PHY** |
| Dependencies | Espressif SDK | **✓ Zero extra** |

---

## Why not Zigbee / Thread?

```
Zigbee / Thread:
  ✗ Require a special radio chip (802.15.4)
  ✗ Complex — hundreds of KB of code
  ✗ Difficult integration
  ✗ Certification required

µMesh:
  ✓ Runs on the ESP32 you already have
  ✓ Lightweight — tens of KB
  ✓ #include and go
  ✓ MIT license
```

---

## How it works

µMesh sends its own packets directly through the WiFi hardware using `esp_wifi_80211_tx()` — no association, no AP, no DHCP. It receives through promiscuous mode. The WiFi antenna becomes a **general-purpose radio transmitter** for the protocol.

```
Standard ESP32 WiFi stack:
  Application -> lwIP -> WiFi stack -> 802.11 -> Antenna

µMesh:
  Application -> µMesh stack -> raw 802.11 frame -> Antenna
                                (bypasses entire WiFi stack)
```

---

## Quick Start

```c
#include "umesh.h"

static const uint8_t KEY[16] = { 0x2B, 0x7E, ... };

umesh_cfg_t cfg = {
    .net_id     = 0x01,
    .node_id    = UMESH_ADDR_UNASSIGNED,  /* auto-assign */
    .master_key = KEY,
    .role       = UMESH_ROLE_END_NODE,
    .security   = UMESH_SEC_FULL,
    .channel    = 6,
};

void on_temp(umesh_pkt_t *pkt) {
    float t = *(float *)pkt->payload;
    printf("Temperature: %.1f C\n", t);
}

void app_main(void) {
    umesh_init(&cfg);
    umesh_start();
    umesh_on_cmd(UMESH_CMD_SENSOR_TEMP, on_temp);

    float temp = 23.5f;
    umesh_send(0x02, UMESH_CMD_SENSOR_TEMP,
               &temp, sizeof(temp), UMESH_FLAG_ACK_REQ);
}
```

---

## Sensor Networks (Gradient Routing)

For many-to-one sensor collection (for example 30 sensors -> 1 coordinator),
enable gradient mode:

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

In this mode each node tracks distance-to-coordinator and forwards packets
uphill through neighbors with lower distance.

---

## Architecture

```
+---------------------------------------------+
|              APPLICATION LAYER              |
|           umesh_send / receive              |
+---------------------------------------------+
|             NETWORK LAYER                   |
|      addressing, routing, multi-hop         |
+---------------------------------------------+
|               MAC LAYER                     |
|         CSMA/CA, ACK, backoff               |
+------------------+--------------------------+
|  SECURITY LAYER  |      FEC LAYER           |
|  AES-128 CTR     |    Hamming(7,4)          |
|  HMAC-SHA256     |                          |
+------------------+--------------------------+
|             PHYSICAL LAYER                  |
|    Raw IEEE 802.11 frames / ESP32 WiFi      |
|    esp_wifi_80211_tx / Promiscuous mode     |
+---------------------------------------------+
```

---

## Performance

```
Range:            ~200 m (direct link)
                  ~500 m+ (multi-hop, 3 hops)
Latency:          ~1-5 ms (1 hop)
Data rate:        tens of kbps (raw 802.11)
Max nodes:        16 per network
Max hops:         15
TX current:       ~80 mA @ 3.3V
RX current:       ~60 mA @ 3.3V
```

---

## Comparison

| | µMesh | ESP-NOW | Zigbee | BLE Mesh | LoRa |
|---|---|---|---|---|---|
| Range | 200m+ | 200m | 100m | 30m | 10km |
| Multi-hop | ✓ | ✗ | ✓ | ✓ | ✗ |
| Extra HW | ✗ | ✗ | ✓ | ✗ | ✓ |
| Open source | ✓ | ✗ | ~ | ~ | ✓ |
| C99 embedded | ✓ | ✗ | ✗ | ✗ | ~ |
| Zero deps | ✓ | ✗ | ✗ | ✗ | ✗ |
| Mesh routing | ✓ | ✗ | ✓ | ✓ | ✗ |

---

## Hardware

µMesh runs on **any ESP32** you already have:

| Chip | Support | Notes |
|---|---|---|
| ESP32 (classic) | ✓ | Full support |
| ESP32-S3 | ✓ | Recommended |
| ESP32-C3 | ✓ | Cheapest (~EUR 3) |
| ESP32-S2 | ✓ | Single-core |

**No extra hardware.** Just ESP32 + power supply.

---

## Documentation

| Document | Content |
|---|---|
| [DESIGN.md](docs/DESIGN.md) | Architectural decisions and rationale |
| [API_REFERENCE.md](docs/API_REFERENCE.md) | Public API, cfg fields and callbacks |
| [PHYSICAL_LAYER.md](docs/PHYSICAL_LAYER.md) | Raw 802.11, promiscuous mode, ESP32 WiFi API |
| [MAC_LAYER.md](docs/MAC_LAYER.md) | CSMA/CA, ACK, backoff, collisions |
| [NETWORK_LAYER.md](docs/NETWORK_LAYER.md) | Routing, discovery, multi-hop |
| [SECURITY_LAYER.md](docs/SECURITY_LAYER.md) | AES-128 CTR, HMAC, replay protection |
| [IMPLEMENTATION.md](docs/IMPLEMENTATION.md) | C99 structure, project layout |
| [KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) | Known issues and limitations |
| [POWER_MANAGEMENT.md](docs/POWER_MANAGEMENT.md) | ACTIVE/LIGHT/DEEP power profiles |

---

## micro-toolkit Integration

µMesh uses **microcrypt** for cryptographic primitives (AES-128, HMAC-SHA256).
Other micro-toolkit libraries are optional and can be integrated if needed:

| Library | Role in µMesh |
|---|---|
| `microcrypt` | AES-128, HMAC-SHA256 — **used** |
| `microfsm` | Network layer FSM (optional) |
| `microlog` | Structured packet/RSSI logging (optional) |
| `iotspool` | Store-and-forward over µMesh (optional) |
| `microwdt` | Watchdog for protocol stack task (optional) |
| `microtest` | Unit test framework (optional) |

---

## Project Status

```
+ Protocol designed
+ Documentation
+ PHY layer (raw 802.11)
+ MAC layer (CSMA/CA, ACK, backoff)
+ Network layer (routing, discovery, FSM)
+ Security layer (AES-128 CTR, HMAC-SHA256)
+ Auto-mesh coordinator election
+ Gradient routing mode for sensor networks
+ ESP32 port
+ Examples
+ Unit tests (12/12 passing)
```

---

## License

MIT — free for commercial and personal use.

---

## Power Consumption

| Mode | Role | Avg Current | Use Case |
|------|------|-------------|----------|
| ACTIVE | Any | ~60-80 mA | Development, always-on |
| LIGHT | END_NODE | ~8 mA | Battery sensors, periodic check-in |
| DEEP | END_NODE | ~1-5 mA | Long interval telemetry (30s+) |
| ACTIVE | ROUTER | ~60 mA | Must stay awake to route packets |

Deep sleep is intended for gradient routing deployments.
Routers should use ACTIVE mode in production meshes.
