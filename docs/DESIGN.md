# µMesh — Design Rationale

> This document explains **why** individual decisions are the way they are.
> For descriptions of **what** each layer does, see the corresponding layer documents.

---

## Core Philosophy

```
Complexity inside.
Simplicity outside.

A developer should be able to use µMesh
without knowing what CSMA/CA is
or how raw 802.11 works.
```

Four principles that guide every decision:

1. **Zero dependencies** — no external libraries beyond ESP-IDF and standard C99
2. **Zero dynamic allocation** — no `malloc` at runtime, everything static
3. **Portable API** — same interface regardless of the PHY layer
4. **Simple integration** — `#include "umesh.h"` and four functions

---

## Why raw 802.11 instead of ESP-NOW?

### What ESP-NOW does not provide

ESP-NOW is a great idea but has fundamental limitations:

```
ESP-NOW:
  - Proprietary protocol from Espressif
  - No mesh network layer
  - No multi-hop routing
  - MAC-address-only addressing — no NET_ID
  - Manual peer registration
  - No command layer
  - Basic security without replay protection
  - Locked to the Espressif ecosystem
```

### What µMesh adds

```
µMesh sits on top of raw 802.11
the same way MQTT sits on top of TCP/IP.

802.11 PHY  =  transport
µMesh stack =  everything else

Network layer, routing, discovery,
security, command layer — none of that
exists in ESP-NOW.
```

---

## Why not Zigbee or Thread?

| | Zigbee / Thread | µMesh |
|---|---|---|
| Hardware | Special 802.15.4 chip | ESP32 you already have |
| Stack size | Hundreds of KB | Tens of KB |
| Integration | Complex | `#include` |
| License | Various | MIT |
| Certification | Required | No |

For hobby and embedded projects, Zigbee/Thread is too heavy.

---

## Why connectionless instead of connection-oriented?

### Connection-oriented (like TCP)
```
SYN -> HANDSHAKE -> SESSION -> DATA -> CLOSE
```
Overhead on every connection — unsuitable for MCUs with infrequent packets.

### Connectionless / Beacon (µMesh)
```
[silence] ... PACKET ... [silence] ... PACKET ...
```
- No overhead when idle
- Naturally broadcast-friendly
- Simpler state machine
- More resilient to dropouts

---

## Why TDD instead of FDD?

µMesh uses **a single frequency band** (one WiFi channel) for both TX and RX — Time Division Duplex. This is the same approach as standard 802.11 WiFi. The protocol controls who transmits and when.

---

## Why Hamming(7,4) FEC?

| FEC | Advantages | Disadvantages |
|---|---|---|
| Reed-Solomon | Strong correction | Computationally expensive |
| **Hamming(7,4)** | **Simple, bitwise ops** | **Corrects 1 bit per 7** |
| No FEC | No overhead | No correction |

For raw 802.11 in typical environments, Hamming(7,4) is sufficient. 802.11 has its own CRC and retransmissions at the physical layer.

---

## Why AES-128 CTR?

| Mode | Padding | Overhead | Speed |
|---|---|---|---|
| CBC | Yes | 16B IV | Sequential |
| **CTR** | **No** | **0B (NONCE is free)** | **Parallelizable** |

CTR mode turns a block cipher into a stream cipher. The NONCE is derived from SEQ_NUM — zero extra overhead.

---

## Why truncated HMAC (4 bytes)?

```
Full HMAC-SHA256 = 32 bytes -> excessive overhead
4 bytes = 2^32 combinations
        + SEQ_NUM replay protection
        = practically unbreakable for this use case
```

---

## Why two keys (ENC_KEY + AUTH_KEY)?

Using the same key for both encryption and authentication is cryptographically weak. Separation is standard practice (NIST). In practice, only one MASTER_KEY needs to be shared — both keys are derived from it.

---

## Why distance-vector routing?

| Routing | Advantages | Disadvantages |
|---|---|---|
| Flooding | Simple | Exponential packet count |
| **Distance Vector** | **Simple, low memory** | Slower convergence |
| Link State (OSPF) | Fast convergence | Memory-intensive |

For a small network (max 16 nodes), Distance Vector is ideal. A 16-entry table fits in tens of bytes of RAM.

---

## Why use the BSSID field as NET_ID?

802.11 frames have a mandatory Addr3 / BSSID field. In infrastructure mode this is the AP MAC address. In µMesh we **repurpose** this field as a network identifier:

```
Addr3 = 0xAC 0x00 NET_ID 0x00 0x00 0x00
         ^     ^    ^
      µMesh  ver  network
      prefix
```

Result: devices with a different NET_ID ignore the packet **without extra processing** — the WiFi hardware filters it automatically.

---

## Why include RSSI in the routing metric?

Raw 802.11 provides RSSI for every received packet at no extra cost. It would be wasteful not to use it. A route with fewer hops but weak signal may be worse than a route with more hops and strong signal.

```
metric = hop_count x 10 + rssi_penalty
```

---

## micro-toolkit Integration

µMesh is designed to work **standalone** — without any additional dependencies. It also integrates naturally with micro-toolkit libraries:

µMesh uses **microcrypt** for cryptographic primitives (AES-128, HMAC-SHA256).
Other micro-toolkit libraries are optional and can be integrated if needed:

| Library | Role in µMesh |
|---|---|
| `microcrypt` | AES-128, HMAC-SHA256 — **used** |
| `microfsm` | Network layer FSM (optional) |
| `microlog` | RSSI, packet, routing event logging (optional) |
| `iotspool` | Store-and-forward over µMesh link (optional) |
| `microwdt` | Watchdog for protocol stack task (optional) |
| `microtest` | Unit tests for all layers (optional) |

These dependencies are **optional** — each can be replaced or disabled.
