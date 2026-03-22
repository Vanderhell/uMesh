# µMesh — Network Layer

> Addressing · Routing · Multi-hop · Discovery · Sequence numbers

---

## Overview

The Network layer turns "two devices" into a **real mesh network**:

1. **Addressing** — every node has a unique identity
2. **Node types** — coordinator, router, end node
3. **Routing** — how a packet reaches its destination
4. **Multi-hop** — what if the destination is not directly reachable
5. **Discovery** — how a new node joins the network
6. **Sequence numbers** — protection against duplicates

---

## 1. Addressing

```
Full address = NET_ID + NODE_ID
               8 bit    8 bit
```

### NET_ID

| Value | Meaning |
|---|---|
| `0x00` | Reserved |
| `0x01-0xFE` | User networks |
| `0xFF` | Inter-network broadcast |

NET_ID is also encoded in the 802.11 BSSID field (Addr3) — devices with a different NET_ID are filtered at the hardware level.

### NODE_ID

| Value | Meaning |
|---|---|
| `0x00` | Network broadcast |
| `0x01` | Coordinator |
| `0x02-0xFD` | Regular nodes |
| `0xFE` | Unassigned (new node) |
| `0xFF` | Reserved |

```c
#define UMESH_ADDR_BROADCAST    0x00
#define UMESH_ADDR_COORDINATOR  0x01
#define UMESH_ADDR_UNASSIGNED   0xFE
```

---

## 2. Node types

### COORDINATOR (NODE_ID = 0x01)
```
- One per network
- Assigns NODE_IDs to new nodes
- Maintains the full routing table
- Typically: an ESP32 with permanent power supply
```

### ROUTER
```
- Forwards packets (multi-hop)
- Maintains a partial routing table
- Extends the network range
```

### END NODE
```
- Only sends/receives its own packets
- No routing
- Lowest power consumption
- Does not send ROUTE_UPDATE
```

---

## 3. Topology

```
     +--------------------------------------+
     |          NET_ID = 0x01               |
     |                                      |
     |   [COORD 0x01]--------[NODE 0x02]   |
     |        |                   |         |
     |        |                   |         |
     |   [NODE 0x03]--------[NODE 0x04]   |
     |                                      |
     |   Coordinator routing table:         |
     |   0x02 -> direct,   hop=1, rssi=-55 |
     |   0x03 -> direct,   hop=1, rssi=-62 |
     |   0x04 -> via 0x02, hop=2, rssi=-70 |
     +--------------------------------------+
```

---

## 4. Network Header (2 bytes)

```
+---------------------------------------------+
|  [ HOP_COUNT ][ SEQ_NUM           ]         |
|      4 bit          12 bit                  |
+---------------------------------------------+
```

- **HOP_COUNT** — max 15; each router decrements by 1; dropped at 0
- **SEQ_NUM** — 0-4095; protection against duplicates and replay attacks

---

## 5. Routing — Distance Vector + RSSI

```c
typedef struct {
    uint8_t  dst_node;
    uint8_t  next_hop;
    uint8_t  hop_count;
    int8_t   last_rssi;       /* RSSI of the last received packet */
    uint8_t  metric;          /* hop_count x 10 + rssi_penalty */
    uint32_t last_seen_ms;
} umesh_route_entry_t;

#define UMESH_MAX_ROUTES   16
```

### RSSI metric

```c
uint8_t umesh_route_metric(uint8_t hops, int8_t rssi) {
    uint8_t rssi_penalty = 0;
    if      (rssi > -50) rssi_penalty = 0;
    else if (rssi > -70) rssi_penalty = 2;
    else if (rssi > -85) rssi_penalty = 5;
    else                 rssi_penalty = 10;
    return hops * 10 + rssi_penalty;
}
/* Lower metric = better route */
```

Uses RSSI that raw 802.11 provides at no extra cost.

---

## 6. Discovery — JOIN process

```
New node (0xFE):
  +== CMD_JOIN broadcast ==================> all

Coordinator (0x01):
  <== CMD_JOIN ===========================+
  +== CMD_ASSIGN unicast =================> new node
     (assigned NODE_ID + routing table)

New node:
  <== CMD_ASSIGN ==========================+
  +== CMD_JOIN_ACK ========================> coordinator

Coordinator:
  +== CMD_NODE_JOINED broadcast ===========> all
```

---

## 7. Sequence numbers — Sliding Window

```
last_seq = 100, window = 32 bits

SEQ 101 -> new, accepted              OK
SEQ  99 -> within window, not seen   OK
SEQ  68 -> too old, dropped          X
SEQ 100 -> duplicate, dropped        X
```

```c
#define UMESH_SEQ_WINDOW    32

typedef struct {
    uint8_t  src_node;
    uint16_t last_seq;
    uint32_t window_bitmap;
} umesh_seq_state_t;
```

### SEQ_NUM wrap-around

On overflow (after ~82 seconds of intensive communication at ~50 pkt/s) the SALT for AES-CTR is regenerated:

```c
if (++s_seq_num > 0x0FFF) {
    s_seq_num = 0;
    sec_regenerate_salt();  /* new NONCE base */
}
```

---

## 8. State machine

```
UNINIT
  | umesh_init()
  v
SCANNING <------------------------------+
  | coordinator found / timeout         |
  v                                     |
JOINING                                 |
  | CMD_ASSIGN received                 |
  v                                     |
CONNECTED                               |
  | send / receive                      |
  | ROUTE_UPDATE every 30s              |
  | (COORDINATOR and ROUTER only)       |
  | coordinator lost (90s)              |
  v                                     |
RECONNECTING ---------------------------+
```

---

## 9. Network commands

| Opcode | Hex | Description |
|---|---|---|
| `UMESH_CMD_JOIN` | `0x50` | New node searches for network |
| `UMESH_CMD_ASSIGN` | `0x51` | NODE_ID assignment |
| `UMESH_CMD_LEAVE` | `0x52` | Leave network |
| `UMESH_CMD_DISCOVER` | `0x53` | Node discovery |
| `UMESH_CMD_ROUTE_UPDATE` | `0x54` | Routing table update |
| `UMESH_CMD_NODE_JOINED` | `0x55` | Broadcast — new node |
| `UMESH_CMD_NODE_LEFT` | `0x56` | Broadcast — node left |

---

## 10. Network API (internal)

```c
umesh_result_t net_init(uint8_t net_id, uint8_t node_id,
                        umesh_role_t role);
umesh_result_t net_join(void);
void           net_leave(void);
umesh_result_t net_route(umesh_frame_t *frame);
uint8_t        net_get_node_id(void);
uint8_t        net_get_node_count(void);
void           net_tick(uint32_t now_ms);
```

---

## 11. Parameters

```c
#define UMESH_MAX_NODES           16
#define UMESH_MAX_ROUTES          16
#define UMESH_MAX_HOP_COUNT       15
#define UMESH_ROUTE_UPDATE_MS     30000
#define UMESH_NODE_TIMEOUT_MS     90000
#define UMESH_JOIN_TIMEOUT_MS      5000
#define UMESH_DISCOVER_TIMEOUT_MS  2000
#define UMESH_SEQ_WINDOW           32
```
