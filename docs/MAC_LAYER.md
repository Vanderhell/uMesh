# µMesh — MAC Layer

> CSMA/CA · TDD · Binary Exponential Backoff · ACK · Priority

---

## Overview

The MAC layer is responsible for:

1. **Carrier Sense** — detecting whether the channel is free
2. **Collision avoidance** — how to prevent them
3. **Backoff** — what to do when the channel is busy
4. **ACK** — delivery confirmation
5. **Retransmission** — retry on loss
6. **Priority** — system packets before data packets

---

## 1. Basic principle — CSMA/CA

```
CSMA = Carrier Sense Multiple Access
CA   = Collision Avoidance

Rule:
"Before I speak — I listen.
 If someone is speaking — I wait.
 Then I speak."
```

### State diagram

```
         +------------------------------------------+
         v                                          |
+-----------------+   channel free   +---------------+-+
|   IDLE /        |---------------->|   TRANSMIT      |
|   LISTENING     |                 +--------+--------+
+--------+--------+                          | done
         |                                   v
         |                         +------------------+
         | channel busy            |   WAIT_ACK       |
         v                         +--------+---------+
+-----------------+                         | timeout
|   BACKOFF       |<------------------------+ / no ACK
|   waiting       |    collision / no ACK   |
+--------+--------+                         |
         | backoff expired                  | ACK received
         +--------------------------------->+
                                   +------------------+
                                   |    SUCCESS       |
                                   +------------------+
```

---

## 2. Carrier Sense — CCA

For raw 802.11 on ESP32, CCA is largely **handled in hardware** — the WiFi chip measures channel energy automatically before each transmission.

µMesh adds a **software layer** on top:

```c
#define UMESH_CCA_THRESHOLD   -85   /* dBm */
#define UMESH_CCA_TIME_MS       2   /* measurement window ms */

bool umesh_cca_channel_free(void) {
    /* Check whether reception is in progress
     * via promiscuous callback flag */
    return !s_rx_in_progress &&
           (s_last_rssi < UMESH_CCA_THRESHOLD);
}
```

---

## 3. Slot Timing

```c
#define UMESH_SLOT_TIME_MS      1    /* base unit */

/* Derived:
 * CCA time     = 2 ms
 * ACK timeout  = 50 ms  (raw 802.11 latency ~1-5 ms)
 * Max backoff  = 32 ms  */
```

### Why 50 ms ACK timeout?

```
raw 802.11 latency:   ~1-5 ms
RX processing:        ~2 ms
ACK generation:       ~1 ms
ACK transmission:     ~2 ms
Safety margin:       ~40 ms
----------------------------
ACK timeout:          50 ms
```

---

## 4. Backoff — Binary Exponential

```
Attempt #1  ->  random(0,  3) slots  =  0-3 ms
Attempt #2  ->  random(0,  7) slots  =  0-7 ms
Attempt #3  ->  random(0, 15) slots  =  0-15 ms
Attempt #4  ->  random(0, 31) slots  =  0-31 ms
Attempt #5  ->  ERROR
```

```c
#define UMESH_MAX_RETRIES     4

uint8_t umesh_backoff_slots(uint8_t retry) {
    uint8_t window = (1 << (retry + 2)) - 1;
    return rand() % window;
}
```

Seed from MAC address + uptime — different for each node:

```c
uint8_t mac[6];
esp_wifi_get_mac(WIFI_IF_STA, mac);
srand(mac[5] | (mac[4] << 8) |
      (uint32_t)esp_timer_get_time());
```

---

## 5. ACK Mechanism

### Normal flow

```
Node A                              Node B
  |                                   |
  +-- CCA (2 ms) ------------------->|
  |<-- free -------------------------+|
  |                                   |
  +======== PACKET ==================>+
  |                                   | process (~2 ms)
  |<======== ACK ====================+
  |                                   |
  | SUCCESS  (total ~10 ms)          |
```

### ACK packet — minimal

```
[802.11 HEADER][UMESH HEADER]
No payload
FLAGS bit 1 = 1  ->  "this is an ACK"
Total size:   ~30 bytes
Transmit time: < 1 ms
```

```c
#define UMESH_ACK_TIMEOUT_MS   50
#define UMESH_MAX_RETRIES       4
```

---

## 6. Packet Priority

```c
#define UMESH_FLAG_PRIO_HIGH    (3 << 6)  /* ACK, JOIN, PING */
#define UMESH_FLAG_PRIO_NORMAL  (2 << 6)  /* commands */
#define UMESH_FLAG_PRIO_LOW     (1 << 6)  /* sensor data */
#define UMESH_FLAG_PRIO_BULK    (0 << 6)  /* large transfers */
```

| Priority | Examples | CCA time | Backoff |
|---|---|---|---|
| `PRIO_HIGH` | ACK, PING, JOIN | 1 ms | Half window |
| `PRIO_NORMAL` | SET_*, CMD_* | 2 ms | Standard |
| `PRIO_LOW` | SENSOR_*, RAW | 2 ms | 1.5x standard |
| `PRIO_BULK` | Large transfers | 2 ms | 2x standard |

---

## 7. Broadcast

```
DST_ADDR = 0x00  ->  broadcast
802.11 Addr1 = FF:FF:FF:FF:FF:FF
```

Rules:
- **Never** `ACK_REQUIRED`
- **Always** `FIRE_AND_FORGET`
- Protected against broadcast storm

**Exception — `UMESH_CMD_DISCOVER`:**
Each node replies with a random delay `random(0, 50 ms)`.

---

## 8. Full packet timing

```
+------------------------------------------------------------+
|  COMPLETE FLOW — with ACK                                  |
|                                                            |
|  CCA    PACKET     TURNAROUND  ACK                         |
|  +-2ms-++-5ms----+ +-2ms--+ +-3ms--+                      |
|                                                            |
|  Total:   ~12 ms  with ACK                                 |
|           ~5 ms   without ACK (FIRE_AND_FORGET)            |
|                                                            |
|  Worst case (4 retransmissions + backoff):                 |
|  ~5ms x 4 + ~50ms backoff ~ 70ms                          |
+------------------------------------------------------------+
```

---

## 9. MAC Frame

```c
typedef struct {
    uint8_t  net_id;
    uint8_t  dst;
    uint8_t  src;
    uint8_t  flags;
    uint8_t  payload_len;
    uint16_t seq_num;
    uint8_t  hop_count;
    uint8_t  payload[UMESH_MAX_PAYLOAD];
    uint16_t crc;
} umesh_frame_t;
```

### FLAGS byte

| Bit | Name | Description |
|---|---|---|
| 0 | `UMESH_FLAG_ACK_REQ` | Requires acknowledgement |
| 1 | `UMESH_FLAG_IS_ACK` | This is an ACK packet |
| 2 | `UMESH_FLAG_ENCRYPTED` | Payload is encrypted |
| 3 | `UMESH_FLAG_FRAGMENT` | Fragmented (future) |
| 6-7 | `UMESH_FLAG_PRIO_MASK` | Priority (2 bits) |

---

## 10. MAC API (internal)

```c
mac_result_t mac_send(umesh_frame_t *frame, uint8_t priority);
void         mac_set_rx_callback(void (*cb)(umesh_frame_t *frame));
bool         mac_channel_is_free(void);
mac_stats_t  mac_get_stats(void);
```

---

## 11. Constants

```c
#define UMESH_SLOT_TIME_MS      1
#define UMESH_CCA_TIME_MS       2
#define UMESH_ACK_TIMEOUT_MS   50
#define UMESH_MAX_RETRIES       4
#define UMESH_CCA_THRESHOLD   -85    /* dBm */
#define UMESH_DISCOVER_BACKOFF_MS  50
```
