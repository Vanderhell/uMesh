# µMesh

> **Lightweight open mesh protocol over raw 802.11 for ESP32.**
> No router. No infrastructure. No proprietary dependencies.

[![Language: C99](https://img.shields.io/badge/language-C99-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](#)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3%20%7C%20ESP32--C3-lightgrey.svg)](#)
[![Status: v1.0.0](https://img.shields.io/badge/status-v1.0.0-brightgreen.svg)](#)

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
  - Require a special radio chip (802.15.4)
  - Complex — hundreds of KB of code
  - Difficult integration
  - Certification required

µMesh:
  + Runs on the ESP32 you already have
  + Lightweight — tens of KB
  + #include and go
  + MIT license
```

---

## How it works

µMesh sends packets directly through the WiFi hardware using `esp_wifi_80211_tx()` — no association, no AP, no DHCP. It receives through promiscuous mode. The WiFi antenna becomes a **general-purpose radio transmitter** for the protocol.

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

void app_main(void) {
    umesh_init(&cfg);
    umesh_start();

    float temp = 23.5f;
    umesh_send(0x02, UMESH_CMD_SENSOR_TEMP,
               &temp, sizeof(temp), UMESH_FLAG_ACK_REQ);
}
```

---

## Architecture

```
+---------------------------------------------+
|              APPLICATION LAYER              |
+---------------------------------------------+
|             NETWORK LAYER                   |
|      addressing, routing, multi-hop         |
+---------------------------------------------+
|               MAC LAYER                     |
|         CSMA/CA, ACK, backoff               |
+------------------+--------------------------+
|  SECURITY LAYER  |      FEC LAYER           |
|  AES-128 CTR     |    Hamming(7,4)          |
+------------------+--------------------------+
|             PHYSICAL LAYER                  |
|    Raw IEEE 802.11 / ESP32 WiFi HAL         |
+---------------------------------------------+
```

---

## micro-toolkit Integration

µMesh uses **microcrypt** for cryptographic primitives (AES-128, HMAC-SHA256).
Other micro-toolkit libraries are optional:

| Library | Role |
|---|---|
| `microcrypt` | AES-128, HMAC-SHA256 — **used** |
| `microfsm` | Network layer FSM (optional) |
| `microlog` | Structured logging (optional) |
| `iotspool` | Store-and-forward over µMesh (optional) |
| `microwdt` | Watchdog for protocol stack (optional) |
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
+ ESP32 port
+ Examples
+ Unit tests (9/9 passing)
```

---

## License

MIT — free for commercial and personal use.
