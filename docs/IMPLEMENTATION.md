# µMesh — Implementation Guide

> C99 skeleton · Directory structure · ESP32 port · Implementation order

---

## Directory structure

```
umesh/
|
+-- include/
|   +-- umesh.h                    # single public header
|
+-- src/
|   +-- umesh.c                    # init, start, stop, API glue
|   |
|   +-- phy/
|   |   +-- phy.h / phy.c          # PHY coordination layer
|   |   +-- phy_hal.h              # HAL interface (header only)
|   |
|   +-- mac/
|   |   +-- mac.h / mac.c          # CSMA/CA, backoff, retry
|   |   +-- cca.h / cca.c          # carrier sense
|   |   +-- frame.h / frame.c      # packet serialize/deserialize
|   |
|   +-- net/
|   |   +-- net.h / net.c          # routing FSM, JOIN
|   |   +-- routing.h / routing.c  # routing table + RSSI metric
|   |   +-- discovery.h / .c       # JOIN, ASSIGN, LEAVE
|   |
|   +-- sec/
|   |   +-- sec.h / sec.c          # encrypt/decrypt, MIC
|   |   +-- keys.h / keys.c        # key derivation
|   |
|   +-- common/
|       +-- defs.h                 # types, constants, macros
|       +-- crc.h / crc.c          # CRC16
|       +-- ring.h / ring.c        # ISR-safe ring buffer
|
+-- port/
|   +-- esp32/
|   |   +-- phy_esp32.c            # raw 802.11, promiscuous mode
|   +-- posix/
|       +-- phy_posix.c            # simulation for PC tests
|
+-- tests/
|   +-- test_crc.c
|   +-- test_ring.c
|   +-- test_frame.c
|   +-- test_sec.c
|   +-- test_posix_loopback.c
|   +-- test_mac.c
|   +-- test_routing.c
|   +-- test_net.c
|   +-- test_e2e.c
|   +-- CMakeLists.txt
|
+-- examples/
|   +-- coordinator/
|   |   +-- main.c
|   +-- end_node/
|       +-- main.c
|
+-- docs/
|   +-- (this documentation)
|
+-- CMakeLists.txt
+-- README.md
+-- CHANGELOG.md
+-- LICENSE
```

---

## common/defs.h

```c
#ifndef UMESH_DEFS_H
#define UMESH_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Version ------------------------------------------------- */
#define UMESH_VERSION_MAJOR    1
#define UMESH_VERSION_MINOR    0
#define UMESH_VERSION_PATCH    0

/* ---- Addresses ----------------------------------------------- */
#define UMESH_ADDR_BROADCAST   0x00
#define UMESH_ADDR_COORDINATOR 0x01
#define UMESH_ADDR_UNASSIGNED  0xFE

/* ---- Network ------------------------------------------------- */
#define UMESH_MAX_NODES        16
#define UMESH_MAX_PAYLOAD      64
#define UMESH_MAX_ROUTES       16
#define UMESH_MAX_HOP_COUNT    15
#define UMESH_SEQ_WINDOW       32

/* ---- WiFi PHY ------------------------------------------------ */
#define UMESH_DEFAULT_CHANNEL   6
#define UMESH_80211_HEADER_LEN 24
#define UMESH_MAX_FRAME_SIZE  256
#define UMESH_BSSID_PREFIX_0  0xAC
#define UMESH_BSSID_PREFIX_1  0x00

/* ---- MAC ----------------------------------------------------- */
#define UMESH_SLOT_TIME_MS      1
#define UMESH_CCA_TIME_MS       2
#define UMESH_ACK_TIMEOUT_MS   50
#define UMESH_MAX_RETRIES       4
#define UMESH_CCA_THRESHOLD   -85

/* ---- Security ----------------------------------------------- */
#define UMESH_KEY_SIZE         16
#define UMESH_MIC_SIZE          4

/* ---- Timing ------------------------------------------------- */
#define UMESH_ROUTE_UPDATE_MS  30000
#define UMESH_NODE_TIMEOUT_MS  90000
#define UMESH_JOIN_TIMEOUT_MS   5000

/* ---- Types -------------------------------------------------- */
typedef enum {
    UMESH_ROLE_COORDINATOR = 0,
    UMESH_ROLE_ROUTER      = 1,
    UMESH_ROLE_END_NODE    = 2,
} umesh_role_t;

typedef enum {
    UMESH_STATE_UNINIT       = 0,
    UMESH_STATE_SCANNING     = 1,
    UMESH_STATE_JOINING      = 2,
    UMESH_STATE_CONNECTED    = 3,
    UMESH_STATE_DISCONNECTED = 4,
} umesh_state_t;

typedef enum {
    UMESH_SEC_NONE = 0x00,
    UMESH_SEC_AUTH = 0x01,
    UMESH_SEC_FULL = 0x02,
} umesh_security_t;

/* ---- FLAGS -------------------------------------------------- */
#define UMESH_FLAG_ACK_REQ     (1 << 0)
#define UMESH_FLAG_IS_ACK      (1 << 1)
#define UMESH_FLAG_ENCRYPTED   (1 << 2)
#define UMESH_FLAG_FRAGMENT    (1 << 3)
#define UMESH_FLAG_PRIO_MASK   (3 << 6)
#define UMESH_FLAG_PRIO_HIGH   (3 << 6)
#define UMESH_FLAG_PRIO_NORMAL (2 << 6)
#define UMESH_FLAG_PRIO_LOW    (1 << 6)

/* ---- Return codes ------------------------------------------- */
typedef enum {
    UMESH_OK                   =  0,
    UMESH_ERR_NO_ACK           =  1,
    UMESH_ERR_CHANNEL_BUSY     =  2,
    UMESH_ERR_MAX_RETRIES      =  3,
    UMESH_ERR_NOT_ROUTABLE     =  4,
    UMESH_ERR_NOT_JOINED       =  5,
    UMESH_ERR_TIMEOUT          =  6,
    UMESH_ERR_INVALID_DST      =  7,
    UMESH_ERR_TOO_LONG         =  8,
    UMESH_ERR_NULL_PTR         =  9,
    UMESH_ERR_NOT_INIT         = 10,
    UMESH_ERR_HARDWARE         = 11,
    UMESH_ERR_MIC_FAIL         = 12,
    UMESH_ERR_REPLAY           = 13,
} umesh_result_t;

/* ---- Opcode table ------------------------------------------- */
typedef enum {
    UMESH_CMD_PING          = 0x01,
    UMESH_CMD_PONG          = 0x02,
    UMESH_CMD_RESET         = 0x03,
    UMESH_CMD_STATUS        = 0x04,
    UMESH_CMD_SENSOR_TEMP   = 0x10,
    UMESH_CMD_SENSOR_HUM    = 0x11,
    UMESH_CMD_SENSOR_PRESS  = 0x12,
    UMESH_CMD_SENSOR_RAW    = 0x13,
    UMESH_CMD_SET_INTERVAL  = 0x30,
    UMESH_CMD_SET_THRESHOLD = 0x31,
    UMESH_CMD_SET_MODE      = 0x32,
    UMESH_CMD_JOIN          = 0x50,
    UMESH_CMD_ASSIGN        = 0x51,
    UMESH_CMD_LEAVE         = 0x52,
    UMESH_CMD_DISCOVER      = 0x53,
    UMESH_CMD_ROUTE_UPDATE  = 0x54,
    UMESH_CMD_NODE_JOINED   = 0x55,
    UMESH_CMD_NODE_LEFT     = 0x56,
    UMESH_CMD_USER_BASE     = 0xE0,
    UMESH_CMD_RAW           = 0xFF,
} umesh_cmd_t;

/* ---- Internal frame ----------------------------------------- */
typedef struct {
    uint8_t  net_id;
    uint8_t  dst;
    uint8_t  src;
    uint8_t  flags;
    uint8_t  payload_len;
    uint16_t seq_num;
    uint8_t  hop_count;
    uint8_t  payload[UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE];
    uint16_t crc;
} umesh_frame_t;

/* ---- Utility macros ----------------------------------------- */
#define UMESH_ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define UMESH_UNUSED(x)      (void)(x)
#define UMESH_MIN(a,b)       ((a) < (b) ? (a) : (b))
#define UMESH_MAX(a,b)       ((a) > (b) ? (a) : (b))

#define UMESH_HOP_FROM_SEQ(s)  (((s) >> 12) & 0x0F)
#define UMESH_SEQ_FROM_SEQ(s)  ((s) & 0x0FFF)

#endif /* UMESH_DEFS_H */
```

---

## phy/phy_hal.h — HAL interface

```c
#ifndef UMESH_PHY_HAL_H
#define UMESH_PHY_HAL_H

#include "../common/defs.h"

typedef struct {
    uint8_t channel;    /* WiFi channel 1-13 */
    uint8_t tx_power;   /* 0-78 (= 0-19.5 dBm) */
    uint8_t net_id;     /* for BSSID filter */
} umesh_phy_cfg_t;

/* Each port implements these functions */
umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg);
umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len);
void           phy_hal_set_rx_cb(void (*cb)(const uint8_t *payload,
                                             uint8_t len,
                                             int8_t rssi));
void           phy_hal_deinit(void);

#endif
```

---

## port/esp32/phy_esp32.c — skeleton

```c
#include "../../src/phy/phy_hal.h"
#include "esp_wifi.h"
#include "esp_event.h"

static void (*s_rx_cb)(const uint8_t*, uint8_t, int8_t) = NULL;
static uint8_t s_net_id = 0;

/* 802.11 frame header template */
static const uint8_t S_80211_HDR[] = {
    0x08, 0x00,                          /* Frame Control: Data */
    0x00, 0x00,                          /* Duration */
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,       /* Addr1: Broadcast */
    0x00,0x00,0x00,0x00,0x00,0x00,       /* Addr2: SRC (filled in) */
    0xAC,0x00,0x00,0x00,0x00,0x00,       /* Addr3: BSSID/NET_ID */
    0x00, 0x00                           /* Sequence control */
};

static void IRAM_ATTR promisc_cb(
    void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_DATA) return;

    wifi_promiscuous_pkt_t *pkt = buf;
    uint8_t *frame = pkt->payload;

    /* Quick filter — µMesh prefix + NET_ID */
    if (frame[16] != 0xAC) return;
    if (frame[17] != 0x00) return;
    if (frame[18] != s_net_id) return;

    if (s_rx_cb) {
        s_rx_cb(&frame[24],
                pkt->rx_ctrl.sig_len - 24,
                pkt->rx_ctrl.rssi);
    }
}

umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg) {
    s_net_id = cfg->net_id;

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wcfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(cfg->channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_max_tx_power(cfg->tx_power);

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    esp_wifi_set_promiscuous(true);

    return UMESH_OK;
}

umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len) {
    uint8_t frame[UMESH_MAX_FRAME_SIZE];

    memcpy(frame, S_80211_HDR, 24);

    /* Fill SRC MAC (Addr2) */
    esp_wifi_get_mac(WIFI_IF_STA, &frame[10]);

    /* Fill NET_ID (Addr3 byte 2) */
    frame[18] = s_net_id;

    /* Payload */
    memcpy(&frame[24], payload, len);

    esp_err_t err = esp_wifi_80211_tx(
        WIFI_IF_STA, frame, 24 + len, true
    );
    return (err == ESP_OK) ? UMESH_OK : UMESH_ERR_HARDWARE;
}

void phy_hal_set_rx_cb(void (*cb)(const uint8_t*, uint8_t, int8_t)) {
    s_rx_cb = cb;
}
```

---

## Implementation order

```
Step 1:  common/defs.h        <- types, constants
Step 2:  common/crc.c         <- CRC16 + tests
Step 3:  common/ring.c        <- ring buffer + tests
Step 4:  port/posix/          <- PC simulation
Step 5:  mac/frame.c          <- serialize/deserialize + tests
Step 6:  mac/cca.c            <- carrier sense
Step 7:  mac/mac.c            <- CSMA/CA, ACK + tests
Step 8:  sec/keys.c           <- key derivation + tests
Step 9:  sec/sec.c            <- AES-CTR, HMAC + tests
Step 10: net/routing.c        <- routing table + tests
Step 11: net/discovery.c      <- JOIN/ASSIGN/LEAVE
Step 12: net/net.c            <- network FSM + tests
Step 13: umesh.c              <- API glue
Step 14: port/esp32/          <- ESP32 WiFi port
Step 15: examples/            <- examples
```

---

## Testing on PC

```bash
# POSIX port -- simulation without hardware
cmake -S . -B build -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(umesh C)
set(CMAKE_C_STANDARD 99)

set(UMESH_SOURCES
    src/umesh.c
    src/phy/phy.c
    src/mac/mac.c
    src/mac/cca.c
    src/mac/frame.c
    src/net/net.c
    src/net/routing.c
    src/net/discovery.c
    src/sec/sec.c
    src/sec/keys.c
    src/common/crc.c
    src/common/ring.c
)

add_library(umesh STATIC ${UMESH_SOURCES})
target_include_directories(umesh PUBLIC  include)
target_include_directories(umesh PRIVATE src)

if(UMESH_PORT STREQUAL "esp32")
    target_sources(umesh PRIVATE port/esp32/phy_esp32.c)
else()
    target_sources(umesh PRIVATE port/posix/phy_posix.c)
    message(STATUS "umesh: POSIX port (testing)")
endif()

option(UMESH_BUILD_TESTS "Build tests" ON)
if(UMESH_BUILD_TESTS AND NOT UMESH_PORT STREQUAL "esp32")
    enable_testing()
    add_subdirectory(tests)
endif()
```

---

## micro-toolkit integration (optional)

µMesh uses **microcrypt** for cryptographic primitives (AES-128, HMAC-SHA256).
Other micro-toolkit libraries are optional:

```c
/* microfsm -- network state machine */
#ifdef UMESH_USE_MICROFSM
    #include "microfsm/mfsm.h"
#endif

/* microlog -- structured logging */
#ifdef UMESH_USE_MICROLOG
    #include "microlog/mlog.h"
    #define UMESH_LOG_DEBUG(fmt, ...) mlog_debug("UMESH", fmt, ##__VA_ARGS__)
    #define UMESH_LOG_WARN(fmt, ...)  mlog_warn ("UMESH", fmt, ##__VA_ARGS__)
#else
    #define UMESH_LOG_DEBUG(fmt, ...) (void)0
    #define UMESH_LOG_WARN(fmt, ...)  (void)0
#endif

/* microcrypt -- cryptography (used) */
#ifdef UMESH_USE_MICROCRYPT
    #include "microcrypt/mcrypt.h"
#endif
```
