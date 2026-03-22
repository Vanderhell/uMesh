# µMesh — Physical Layer

> Raw IEEE 802.11 frames · ESP32 WiFi HAL · Promiscuous mode

---

## Overview

The µMesh physical layer uses the **ESP32 WiFi hardware** in a way most developers are unfamiliar with — directly sending and receiving raw 802.11 frames without any WiFi stack involvement.

```
Standard WiFi usage:
  App -> TCP/IP -> WiFi Stack -> 802.11 MAC -> RF -> Antenna
  (router, DHCP, association, authentication...)

µMesh:
  App -> µMesh -> raw 802.11 frame -> RF -> Antenna
  (no router, no association, no infrastructure)
```

The WiFi antenna becomes a **general-purpose radio transmitter.**

---

## 1. How raw 802.11 works on ESP32

### Two key API calls

```c
/* Send a raw frame */
esp_err_t esp_wifi_80211_tx(
    wifi_interface_t ifx,   /* WIFI_IF_STA or WIFI_IF_AP */
    const void *buffer,     /* raw 802.11 frame */
    int len,                /* length in bytes */
    bool en_sys_seq         /* true = ESP manages sequence number */
);

/* Reception — promiscuous mode */
esp_err_t esp_wifi_set_promiscuous(bool en);
esp_err_t esp_wifi_set_promiscuous_rx_cb(
    wifi_promiscuous_cb_t cb  /* callback for every received frame */
);
```

### What this means

```
esp_wifi_80211_tx():
  -> Sends any 802.11 frame
  -> Without AP association
  -> Without WiFi authentication
  -> Directly to RF

Promiscuous mode:
  -> Captures ALL 802.11 frames in range
  -> Not only those addressed to us
  -> We filter by our own criteria
```

---

## 2. Raw 802.11 frame structure

µMesh uses an **802.11 Data frame** with its own payload:

```
+--------------------------------------------------------------+
|                    RAW 802.11 FRAME                          |
|                                                              |
|  [Frame Ctrl][Duration][Addr1][Addr2][Addr3][Seq][Payload]  |
|       2B         2B      6B     6B     6B    2B     nB      |
|                                                              |
|  Frame Control:  0x0800 (Data frame, no DS)                 |
|  Duration:       0x0000                                      |
|  Addr1 (DST):    Broadcast FF:FF:FF:FF:FF:FF                |
|                  or a specific MAC                           |
|  Addr2 (SRC):    ESP32 MAC address                          |
|  Addr3:          BSSID -- repurposed as µMesh NET_ID        |
|  Seq:            Sequence control (managed by ESP)          |
|  Payload:        µMesh packet                               |
+--------------------------------------------------------------+
```

### Addr3 / BSSID as NET_ID

Creative reuse of an existing field:

```c
/* Addr3 = BSSID in standard WiFi
 * In µMesh = network identifier */

uint8_t bssid[6] = {
    0xAC, 0x00,       /* µMesh prefix (fixed) */
    net_id,           /* NET_ID (1 byte)       */
    0x00, 0x00, 0x00  /* reserved              */
};
```

Devices with a different NET_ID ignore the packet automatically — no extra processing.

---

## 3. WiFi channel

µMesh runs on **one fixed WiFi channel** — configured at initialization:

```c
#define UMESH_DEFAULT_CHANNEL   6    /* 2.4 GHz, channel 6 */

/* Other options:
 * Channel 1:  2412 MHz
 * Channel 6:  2437 MHz  <- recommended (center of band)
 * Channel 11: 2462 MHz  */
```

### Why channel 6?

```
The 2.4 GHz band has 13 channels (in EU).
Channels 1, 6, 11 do not overlap.
Channel 6 is the center -> least interference
from neighboring networks on 1 and 11.
```

### Coexistence with regular WiFi

```
µMesh runs on channel 6.
Other WiFi networks also on channel 6?
-> Yes, interference can occur.

Mitigation:
  1. Configurable channel
  2. µMesh packets are short -> low collision probability
  3. CSMA/CA handles it the same way as regular WiFi
```

---

## 4. WiFi initialization for µMesh

```c
/* µMesh does not need the standard WiFi stack —
 * only basic hardware initialization */

void umesh_phy_init(uint8_t channel) {

    /* 1. Initialize WiFi hardware */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* 2. Set STA mode (required for 80211_tx) */
    esp_wifi_set_mode(WIFI_MODE_STA);

    /* 3. Start WiFi (hardware only, not stack) */
    esp_wifi_start();

    /* 4. Set channel */
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    /* 5. Enable promiscuous mode */
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(umesh_phy_rx_callback);

    /* 6. Filter — capture only Data frames */
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filter);
}
```

---

## 5. Sending a packet

```c
/* Maximum µMesh packet size */
#define UMESH_MAX_FRAME_SIZE   256

/* 802.11 header (24 bytes) */
static const uint8_t UMESH_80211_HEADER[] = {
    0x08, 0x00,              /* Frame Control: Data frame */
    0x00, 0x00,              /* Duration */
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,              /* Addr1: Broadcast */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,              /* Addr2: SRC (filled in) */
    0xAC, 0x00, 0x00,
    0x00, 0x00, 0x00,        /* Addr3: BSSID / NET_ID */
    0x00, 0x00               /* Sequence control */
};

umesh_result_t umesh_phy_send(
    const uint8_t *payload,
    uint8_t        payload_len,
    uint8_t        net_id)
{
    uint8_t frame[UMESH_MAX_FRAME_SIZE];
    uint8_t frame_len = 0;

    /* 1. Copy 802.11 header */
    memcpy(frame, UMESH_80211_HEADER, 24);
    frame_len = 24;

    /* 2. Fill SRC MAC (Addr2) */
    esp_wifi_get_mac(WIFI_IF_STA, &frame[10]);

    /* 3. Fill NET_ID into BSSID (Addr3) */
    frame[16] = 0xAC;
    frame[17] = 0x00;
    frame[18] = net_id;

    /* 4. Append µMesh payload */
    memcpy(&frame[24], payload, payload_len);
    frame_len += payload_len;

    /* 5. Send raw frame */
    esp_err_t err = esp_wifi_80211_tx(
        WIFI_IF_STA, frame, frame_len, false
    );

    return (err == ESP_OK) ? UMESH_OK : UMESH_ERR_HARDWARE;
}
```

---

## 6. Receiving a packet

```c
/* Callback -- called for every captured frame */
void umesh_phy_rx_callback(
    void                       *buf,
    wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_DATA) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t  *frame    = pkt->payload;
    uint16_t  frame_len = pkt->rx_ctrl.sig_len;

    /* Minimum length: 802.11 header (24B) + µMesh header */
    if (frame_len < 24 + UMESH_MIN_HEADER) return;

    /* Verify µMesh NET_ID (Addr3 / BSSID) */
    if (frame[16] != 0xAC) return;   /* not µMesh prefix */
    if (frame[17] != 0x00) return;
    uint8_t net_id = frame[18];
    if (net_id != s_ctx.cfg.net_id) return; /* different network */

    /* RSSI from metadata */
    int8_t rssi = pkt->rx_ctrl.rssi;

    /* Pass µMesh payload to MAC layer */
    umesh_mac_on_frame(
        &frame[24],       /* payload start */
        frame_len - 24,   /* payload length */
        rssi
    );
}
```

---

## 7. RSSI — signal strength

Promiscuous mode provides **RSSI for every received frame:**

```c
int8_t rssi = pkt->rx_ctrl.rssi;
/* Typical values:
 *    0 dBm = maximum strength (direct contact)
 *  -50 dBm = excellent signal
 *  -70 dBm = good signal
 *  -85 dBm = usable signal
 *  -95 dBm = marginal signal
 * -100 dBm = signal lost */
```

RSSI is used in the Network layer for:
- Best-path selection (routing)
- Node failure detection
- Link quality reporting

---

## 8. Performance and range

### TX power configuration

```c
/* ESP32 supports configurable TX power
 * Range: 2 dBm to 20 dBm */

esp_wifi_set_max_tx_power(78);  /* 78 = 19.5 dBm (maximum) */

/* Values:
 *  8 =  2 dBm  (minimum, ~10m)
 * 40 = 10 dBm  (~80m)
 * 78 = 19.5 dBm (maximum, ~200m) */
```

### Real-world range

```
TX power 20 dBm, open space:
  Direct range:     ~200 m
  Through 1 wall:   ~80 m
  Through 2 walls:  ~30 m

With multi-hop (3 nodes):
  Total range:      ~500 m+
```

---

## 9. Carrier Sense — CCA on 802.11

802.11 provides **standardized CCA (Clear Channel Assessment):**

```
802.11 CCA:
  Implemented in hardware by the WiFi chip
  Measures energy on the channel
  Result: channel free / busy

µMesh uses:
  esp_wifi_set_promiscuous() -> listens to channel
  If frame arrives before transmission -> backoff
```

CCA is handled automatically by WiFi hardware. µMesh adds a software layer (see cca.c) to track ongoing reception via the promiscuous callback.

---

## 10. Physical layer limitations

### Regulatory limits

```
2.4 GHz band is regulated:
  Max TX power (EU): 20 dBm (100 mW)
  Max TX power (US): 30 dBm for some channels
  ESP32 maximum:     20 dBm -> within limits for both regions
```

### Coexistence

```
µMesh shares 2.4 GHz with:
  - Regular WiFi (802.11 b/g/n)
  - Bluetooth / BLE
  - Zigbee / Thread (802.15.4)
  - Microwave ovens (2450 MHz)
  - Other µMesh networks on the same channel
```

Mitigation: CSMA/CA in the MAC layer + configurable channel.

### One network = one channel

```
All nodes in a µMesh network must be
on the same WiFi channel.
-> Configured once at deployment.
-> Different networks can use different channels.
```

---

## 11. Constants

```c
/* WiFi configuration */
#define UMESH_DEFAULT_CHANNEL     6
#define UMESH_MIN_CHANNEL         1
#define UMESH_MAX_CHANNEL         13

/* Frame */
#define UMESH_80211_HEADER_LEN    24
#define UMESH_MAX_FRAME_SIZE      256

/* µMesh prefix in BSSID */
#define UMESH_BSSID_PREFIX_0      0xAC
#define UMESH_BSSID_PREFIX_1      0x00

/* TX power */
#define UMESH_TX_POWER_MAX        78    /* 19.5 dBm */
#define UMESH_TX_POWER_DEFAULT    60    /* 15 dBm   */
#define UMESH_TX_POWER_LOW        20    /*  5 dBm   */

/* RSSI thresholds */
#define UMESH_RSSI_EXCELLENT      -50   /* dBm */
#define UMESH_RSSI_GOOD           -70
#define UMESH_RSSI_FAIR           -85
#define UMESH_RSSI_POOR           -95
```
