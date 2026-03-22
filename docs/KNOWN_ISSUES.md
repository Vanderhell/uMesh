# µMesh — Known Issues & Challenges

> Complete record of all identified problems, risks, and challenges.
> This document is updated during development.

---

## Legend

| Symbol | Severity | Description |
|---|---|---|
| 🔴 | **Critical** | Must be resolved before shipping |
| 🟡 | **Medium** | Affects functionality, fixable during development |
| 🟢 | **Low** | Minor issue, easy to fix |
| ⚪ | **Known limitation** | Documented, not addressed in current version |
| ✅ | **Resolved** | Identified and fixed |

---

## Contents

1. [Physical Layer — PHY](#1-physical-layer--phy)
2. [MAC Layer](#2-mac-layer)
3. [Network Layer](#3-network-layer)
4. [Security Layer](#4-security-layer)
5. [ESP32-specific](#5-esp32-specific)
6. [Regulatory and legal](#6-regulatory-and-legal)
7. [Design bugs — fixes](#7-design-bugs--fixes)
8. [Known limitations](#8-known-limitations)

---

## 1. Physical Layer — PHY

---

### 🔴 PHY-01 — Coexistence with regular WiFi

**Problem:**
µMesh runs on the 2.4 GHz band — the same as regular WiFi networks, Bluetooth, Zigbee, and other devices. Raw 802.11 frames can collide with existing WiFi infrastructure.

```
Potential conflicts:
  Regular WiFi network on channel 6
  + µMesh on channel 6
  -> increased collision rate
  -> reduced throughput for both
```

**Proposed solution:**
- Configurable channel — choose the channel with least interference
- Channel scan at startup — automatic selection of the quietest channel
- CSMA/CA in the MAC layer handles collisions in the standard way

```c
/* Channel scan before initialization */
uint8_t umesh_phy_find_best_channel(void);
```

**Status:** Partially addressed — CSMA/CA helps, channel scan to be implemented

---

### 🔴 PHY-02 — Promiscuous mode and power consumption

**Problem:**
Promiscuous mode captures **all** 802.11 frames in range — not just µMesh packets. In a dense WiFi environment the callback can be called hundreds of times per second, increasing CPU and energy consumption.

```
Typical WiFi environment:
  Apartment block: 20-50 APs in range
  Office: 10-30 APs
  Each AP beacon: every 100 ms
  -> 200-500 callbacks/s from beacons alone
```

**Proposed solution:**
- Hardware-level filtering — `wifi_promiscuous_filter_t`
- Filter in callback as early as possible (check only BSSID prefix)
- Consider `esp_wifi_set_promiscuous_ctrl_filter()` for additional filtering

```c
/* Quick filter — first 3 bytes of BSSID */
void umesh_phy_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    uint8_t *frame = ((wifi_promiscuous_pkt_t*)buf)->payload;
    /* Check µMesh prefix only -- return immediately if not matching */
    if (frame[16] != 0xAC || frame[17] != 0x00) return;
    /* ... further processing */
}
```

**Status:** Requires optimization at implementation time

---

### 🟡 PHY-03 — Promiscuous mode and other ESP32 features

**Problem:**
When promiscuous mode is enabled, some ESP32 features may not work correctly or at all:

```
Issues with promiscuous mode active:
  - ESP32 cannot connect to a WiFi AP
  - Bluetooth may have higher latency
  - esp_now functions may interfere
```

**Proposed solution:**
µMesh is **exclusive** — if you use it, you give up standard WiFi. Document clearly.

**Status:** ⚪ Known limitation — document

---

### 🟡 PHY-04 — Channel change at runtime

**Problem:**
All nodes must be on the same channel. If we need to change channel (e.g. due to interference), all nodes must coordinate the change simultaneously — otherwise the network splits.

**Proposed solution:**
- v1.0: fixed channel, configured at deployment
- Future: coordinated channel hopping via coordinator

**Status:** ⚪ Known limitation v1.0

---

### 🟢 PHY-05 — 802.11 frame sequence number

**Problem:**
The 802.11 header contains a Sequence Control field. Some APs or devices may reject or ignore frames with incorrect sequence numbers.

**Solution:**
Use `en_sys_seq = true` in `esp_wifi_80211_tx()` — ESP manages the sequence number automatically.

```c
esp_wifi_80211_tx(WIFI_IF_STA, frame, len, true); /* true = auto seq */
```

**Status:** ✅ Trivial solution

---

## 2. MAC Layer

---

### ✅ MAC-01 — ACK timeout too short

**Problem:**
The initial design had `UMESH_ACK_TIMEOUT_MS = 10 ms`, which was too short given that the timeout must also account for frame processing time at the receiver.

**Fix:**
```c
/* For raw 802.11 — actual latency is ~1-5 ms
 * 50 ms provides a safe margin for processing */
#define UMESH_ACK_TIMEOUT_MS   50
```

**Status:** ✅ Fixed

---

### 🟡 MAC-02 — Hidden Node Problem

**Problem:**
A classic wireless network problem. With the longer range of raw 802.11, the probability is higher.

```
Node A <---200m---> Node B <---200m---> Node C
(A and C cannot hear each other)

A transmits to B, C performs CCA -> cannot hear A -> collision
```

**Proposed solution:**
- v1.0: document as known limitation
- Future: RTS/CTS mechanism

**Status:** ⚪ Known limitation v1.0

---

### 🟡 MAC-03 — CSMA/CA and raw 802.11

**Problem:**
When using `esp_wifi_80211_tx()`, it is not guaranteed that the ESP32 performs a full CSMA/CA before transmission. Behavior depends on the ESP-IDF version.

**Proposed solution:**
- Implement a software CCA check before every `esp_wifi_80211_tx()`
- Measure channel energy before transmission
- If channel busy -> backoff

**Status:** Requires verification of ESP-IDF behavior

---

### 🟢 MAC-04 — Backoff seed

**Problem:**
`rand()` without a seed generates the same sequence after restart -> increased collision rate when multiple nodes restart simultaneously.

**Solution:**
```c
/* Seed from MAC address + uptime */
uint8_t mac[6];
esp_wifi_get_mac(WIFI_IF_STA, mac);
srand(mac[5] | (mac[4] << 8) | (uint32_t)esp_timer_get_time());
```

**Status:** 🟢 Simple solution

---

## 3. Network Layer

---

### ⚪ NET-01 — Coordinator: Single Point of Failure

**Problem:**
If the coordinator goes down, new nodes cannot join. Existing nodes continue to communicate until their routing entries expire.

**Status:** ⚪ Known limitation v1.0 — document

---

### ✅ NET-02 — END_NODE and ROUTE_UPDATE

**Problem:**
END_NODE has nothing to report in the routing table, but the original design sent it anyway.

**Fix:**
END_NODE does not send ROUTE_UPDATE — only COORDINATOR and ROUTER do.

**Status:** ✅ Fixed in design

---

### 🟡 NET-03 — SEQ_NUM wrap-around

**Problem:**
SEQ_NUM is 12 bits = 4096 values. At ~50 pkt/s on raw 802.11 this wraps around in ~82 seconds.

```
Raw 802.11 speed: tens of packets/s
4096 / 50 pkt/s = ~82 seconds -> overflow
```

**Solution:**
Modular arithmetic in sliding window + session refresh on overflow.

**Status:** Implemented (see net.c `next_seq()`)

---

### 🟢 NET-04 — Routing table and RSSI

**Enhancement:**
With raw 802.11, we get RSSI for every received packet. The routing table should prefer paths with better RSSI — not just the minimum hop count.

```c
/* metric = hop_count * 10 + rssi_penalty
 * rssi_penalty = 0 for RSSI > -70, increases for weaker signal */
uint8_t umesh_route_metric(uint8_t hops, int8_t rssi);
```

**Status:** 🟢 Enhancement for v1.1

---

## 4. Security Layer

---

### 🔴 SEC-01 — Pre-shared key: entire network

**Problem:**
Compromising the MASTER_KEY = compromising the entire network.

**Solution for v1.0:**
- ESP32 flash encryption (hardware support)
- Document the risk

**Future:**
- Diffie-Hellman during the JOIN process

**Status:** ⚪ Known limitation v1.0

---

### 🔴 SEC-02 — NONCE reuse on SEQ_NUM overflow

**Problem:**
NONCE is derived from SEQ_NUM — on overflow it repeats -> keystream reuse -> critical AES-CTR vulnerability.

**Solution:**
```c
/* On SEQ_NUM overflow -> regenerate SALT */
if (seq_num_will_overflow()) {
    sec_regenerate_salt();
}
```

**Status:** ✅ Implemented (BUGFIX-03)

---

### 🟡 SEC-03 — Timing attack on MIC

**Problem:**
Naive `memcmp()` for MIC comparison is vulnerable to timing attacks.

**Solution:**
```c
bool mic_verify_ct(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}
```

**Status:** 🟢 Simple solution — implemented

---

### ⚪ SEC-04 — Passive eavesdropping

**Problem:**
Promiscuous mode is bidirectional — an attacker with an ESP32 can capture µMesh packets the same way we capture others.

```
Attacker with ESP32 in promiscuous mode:
  -> Sees all µMesh frames
  -> Without key: sees encrypted payload
  -> With key: sees everything
```

**Status:** ⚪ AES-128 CTR handles confidentiality — acceptable

---

### ⚪ SEC-05 — Denial of Service — jamming

**Problem:**
An attacker can broadcast constant interference on the 2.4 GHz channel. Not solvable in software.

**Status:** ⚪ Known limitation — physical layer

---

## 5. ESP32-specific

---

### 🔴 ESP-01 — esp_wifi_80211_tx() limitations

**Problem:**
`esp_wifi_80211_tx()` has several documented limitations:

```
- Maximum frame size: 1500 bytes
  -> µMesh limit 256B is fine

- Requires WiFi in STA or AP mode
  -> We initialize STA mode

- CSMA/CA is not guaranteed
  -> We implement our own

- In some ESP-IDF versions
  may be unstable
  -> Test on target version
```

**Status:** Verify on target ESP-IDF version

---

### 🟡 ESP-02 — ESP-IDF version

**Problem:**
The raw 802.11 API has changed between ESP-IDF versions. What works on v4.4 may not work the same way on v5.x.

**Proposed solution:**
- Document the minimum supported ESP-IDF version
- Test on ESP-IDF v5.1+ (current stable)
- HAL abstraction for potential differences

```c
/* port/esp32/phy_esp32.c -- isolated from the rest of the stack
 * Easy to swap when ESP-IDF changes */
```

**Status:** Define minimum version before shipping

---

### 🟡 ESP-03 — WiFi and Bluetooth concurrency

**Problem:**
ESP32 shares the antenna between WiFi and Bluetooth. Using both simultaneously degrades performance.

```
µMesh (WiFi) + BLE simultaneously:
  -> Increased latency
  -> Reduced range
  -> Higher power consumption
```

**Status:** ⚪ Known limitation — document

---

### 🟢 ESP-04 — Watchdog and promiscuous callback

**Problem:**
If the promiscuous callback takes too long, it can trigger the ESP32 watchdog timer.

**Solution:**
The callback must be as short as possible — just a quick filter and enqueue. Processing in a separate task.

```c
/* Callback -- quick filter and push to queue */
void IRAM_ATTR umesh_phy_rx_cb(void *buf, ...) {
    if (!is_umesh_frame(buf)) return;
    xQueueSendFromISR(s_rx_queue, buf, NULL);
}

/* Processing in task */
void umesh_rx_task(void *arg) {
    while(1) {
        xQueueReceive(s_rx_queue, &frame, portMAX_DELAY);
        umesh_mac_on_frame(&frame);
    }
}
```

**Status:** 🟢 Standard ESP32 practice

---

## 6. Regulatory and legal

---

### 🟡 REG-01 — 2.4 GHz regulation

**Problem:**
Transmission on 2.4 GHz is regulated in every country. ESP32 within its allowed limits is fine, but:

```
EU (ETSI):  Max 20 dBm (100 mW) -> ESP32 max 20 dBm OK
US (FCC):   Depends on channel -> ESP32 within limits OK
Other:      Check local regulations
```

ESP32 is certified for most regions — using it within its parameters is legal.

**Status:** ✅ ESP32 certification covers normal use

---

### ⚪ REG-02 — Raw frames and the WiFi standard

**Problem:**
Sending raw 802.11 frames is technically not compliant with the IEEE 802.11 standard — frames lack proper AP association.

In practice this is a non-issue — the same applies to ESP-NOW, Wireshark monitor mode, etc.

**Status:** ⚪ Academic concern — irrelevant in practice

---

## 7. Design bugs — fixes

---

### ✅ BUGFIX-01 — ACK_TIMEOUT

```c
/* BEFORE (initial design — too conservative): */
#define UMESH_ACK_TIMEOUT_MS   400

/* AFTER (raw 802.11 latency ~1-5 ms): */
#define UMESH_ACK_TIMEOUT_MS   50
```

---

### ✅ BUGFIX-02 — END_NODE ROUTE_UPDATE

END_NODE does not send ROUTE_UPDATE — only COORDINATOR and ROUTER.

---

### ✅ BUGFIX-03 — SEQ_NUM wrap-around handler

Implemented wrap-around detection and SALT refresh in `net.c:next_seq()`.

---

### ✅ BUGFIX-04 — CCA

For raw 802.11, CCA is handled in hardware by the WiFi chip. µMesh adds a lightweight software layer on top (cca.c) tracking the in-progress reception flag and last RSSI.

---

### ✅ BUGFIX-05 — Guard interval

Not applicable for raw 802.11 — 802.11 uses OFDM which inherently handles multipath and inter-symbol interference.

---

## 8. Known limitations

These limitations are **accepted** in the current version:

```
Coordinator SPOF         -> distributed election in future
Fixed channel            -> channel hopping in future
Pre-shared key           -> DH key exchange in future
ESP32 only               -> other platforms in future
Promiscuous = no WiFi    -> document
Hidden node problem      -> RTS/CTS in future
Passive eavesdropping    -> AES-CTR protects payload
DoS jamming              -> physical layer, unsolvable
WiFi + BT degradation    -> document
```

---

## Summary — issue status

| ID | Severity | Status | Description |
|---|---|---|---|
| PHY-01 | 🔴 | ⚠️ | WiFi coexistence |
| PHY-02 | 🔴 | ⚠️ | Promiscuous mode power |
| PHY-03 | 🟡 | ⚪ | Promiscuous and other features |
| PHY-04 | 🟡 | ⚪ | Channel change at runtime |
| PHY-05 | 🟢 | ✅ | 802.11 sequence number |
| MAC-01 | 🔴 | ✅ | ACK timeout fix |
| MAC-02 | ⚪ | ⚪ | Hidden node problem |
| MAC-03 | 🟡 | ⚠️ | CSMA/CA and raw 802.11 |
| MAC-04 | 🟢 | 🟢 | Backoff seed |
| NET-01 | ⚪ | ⚪ | Coordinator SPOF |
| NET-02 | 🟡 | ✅ | END_NODE ROUTE_UPDATE |
| NET-03 | 🟡 | ✅ | SEQ_NUM wrap-around |
| NET-04 | 🟢 | 🟢 | Routing and RSSI |
| SEC-01 | 🔴 | ⚪ | Pre-shared key |
| SEC-02 | 🔴 | ✅ | NONCE reuse |
| SEC-03 | 🟡 | 🟢 | Timing attack on MIC |
| SEC-04 | ⚪ | ⚪ | Passive eavesdropping |
| SEC-05 | ⚪ | ⚪ | DoS jamming |
| ESP-01 | 🔴 | ⚠️ | 80211_tx limitations |
| ESP-02 | 🟡 | ⚠️ | ESP-IDF version |
| ESP-03 | 🟡 | ⚪ | WiFi + BT coexistence |
| ESP-04 | 🟢 | 🟢 | Watchdog and callback |
| REG-01 | 🟡 | ✅ | 2.4 GHz regulation |
| REG-02 | ⚪ | ⚪ | Raw frames and standard |

---

*Last updated: v1.0.0*
*Document version: 3.0*
