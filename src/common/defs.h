#ifndef UMESH_DEFS_H
#define UMESH_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "../../include/umesh_caps.h"

#ifndef UMESH_ENABLE_POWER_MANAGEMENT
#define UMESH_ENABLE_POWER_MANAGEMENT 1
#endif

/* ── Version ───────────────────────────────── */
#define UMESH_VERSION_MAJOR    1
#define UMESH_VERSION_MINOR    4
#define UMESH_VERSION_PATCH    0

/* ── Addresses ─────────────────────────────── */
#define UMESH_ADDR_BROADCAST   0x00
#define UMESH_ADDR_COORDINATOR 0x01
#define UMESH_ADDR_UNASSIGNED  0xFE

/* ── Network ───────────────────────────────── */
#if UMESH_RAM_KB < 400
#define UMESH_MAX_NODES         8
#define UMESH_MAX_ROUTES        8
#define UMESH_MAX_NEIGHBORS     8
#else
#define UMESH_MAX_NODES        16
#define UMESH_MAX_ROUTES       16
#define UMESH_MAX_NEIGHBORS    16
#endif
#define UMESH_MAX_PAYLOAD      64
#define UMESH_MAX_HOP_COUNT    15
#define UMESH_SEQ_WINDOW       32

/* ── WiFi PHY ──────────────────────────────── */
#define UMESH_DEFAULT_CHANNEL   6
#define UMESH_80211_HEADER_LEN 24
#define UMESH_MAX_FRAME_SIZE  256
#define UMESH_BSSID_PREFIX_0  0xAC
#define UMESH_BSSID_PREFIX_1  0x00

/* ── MAC ───────────────────────────────────── */
#define UMESH_SLOT_TIME_MS      1
#define UMESH_CCA_TIME_MS       2
#define UMESH_ACK_TIMEOUT_MS   50
#define UMESH_MAX_RETRIES       4
#define UMESH_CCA_THRESHOLD   -85

/* ── Security ──────────────────────────────── */
#define UMESH_KEY_SIZE         16
#define UMESH_MIC_SIZE          4
#define UMESH_NONCE_SIZE       16

/* ── Timing ────────────────────────────────── */
#define UMESH_ROUTE_UPDATE_MS  30000
#define UMESH_NODE_TIMEOUT_MS  90000
#define UMESH_JOIN_TIMEOUT_MS   5000
#define UMESH_DISCOVER_TIMEOUT_MS 2000
#define UMESH_DISCOVER_BACKOFF_MS 50
#define UMESH_ELECTION_TIMEOUT_MS 1000
#define UMESH_GRADIENT_BEACON_MS 30000
#define UMESH_GRADIENT_JITTER_MAX_MS 200
#define UMESH_NEIGHBOR_TIMEOUT_MS 30000
#define UMESH_POWER_BEACON_MS 1000
#define UMESH_LIGHT_SLEEP_INTERVAL_MS 1000
#define UMESH_LIGHT_LISTEN_WINDOW_MS 100
#define UMESH_DEEP_SLEEP_TX_INTERVAL_MS 30000

/* ── TX power ──────────────────────────────── */
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX     78
#endif
#define UMESH_TX_POWER_DEFAULT 60
#define UMESH_TX_POWER_LOW     20

/* ── RSSI thresholds ───────────────────────── */
#define UMESH_RSSI_EXCELLENT   -50
#define UMESH_RSSI_GOOD        -70
#define UMESH_RSSI_FAIR        -85
#define UMESH_RSSI_POOR        -95

/* ── Role ──────────────────────────────────── */
typedef enum {
    UMESH_ROLE_COORDINATOR = 0,
    UMESH_ROLE_ROUTER      = 1,
    UMESH_ROLE_END_NODE    = 2,
    UMESH_ROLE_AUTO        = 3,
} umesh_role_t;

/* ── State ─────────────────────────────────── */
typedef enum {
    UMESH_STATE_UNINIT       = 0,
    UMESH_STATE_SCANNING     = 1,
    UMESH_STATE_ELECTION     = 2,
    UMESH_STATE_JOINING      = 3,
    UMESH_STATE_CONNECTED    = 4,
    UMESH_STATE_DISCONNECTED = 5,
} umesh_state_t;

/* ── Security level ────────────────────────── */
typedef enum {
    UMESH_SEC_NONE = 0x00,
    UMESH_SEC_AUTH = 0x01,
    UMESH_SEC_FULL = 0x02,
} umesh_security_t;

typedef enum {
    UMESH_ROUTING_DISTANCE_VECTOR = 0,
    UMESH_ROUTING_GRADIENT        = 1,
} umesh_routing_mode_t;

typedef enum {
    UMESH_POWER_ACTIVE = 0,
    UMESH_POWER_LIGHT  = 1,
    UMESH_POWER_DEEP   = 2,
} umesh_power_mode_t;

/* ── FLAGS ─────────────────────────────────── */
#define UMESH_FLAG_ACK_REQ     (1 << 0)
#define UMESH_FLAG_IS_ACK      (1 << 1)
#define UMESH_FLAG_ENCRYPTED   (1 << 2)
#define UMESH_FLAG_FRAGMENT    (1 << 3)
#define UMESH_FLAG_PRIO_MASK   (3 << 6)
#define UMESH_FLAG_PRIO_HIGH   (3 << 6)
#define UMESH_FLAG_PRIO_NORMAL (2 << 6)
#define UMESH_FLAG_PRIO_LOW    (1 << 6)
#define UMESH_FLAG_PRIO_BULK   (0 << 6)

/* Capability flags */
#define UMESH_CAP_WIFI      (1u << 0)
#define UMESH_CAP_BT        (1u << 1)
#define UMESH_CAP_TWT       (1u << 2)
#define UMESH_CAP_POWER_MGT (1u << 3)

/* ── Return codes ──────────────────────────── */
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
    UMESH_ERR_NOT_SUPPORTED    = 14,
} umesh_result_t;

/* ── Opcode table ──────────────────────────── */
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
    UMESH_CMD_ELECTION      = 0x57,
    UMESH_CMD_ELECTION_RESULT = 0x58,
    UMESH_CMD_GRADIENT_BEACON = 0x59,
    UMESH_CMD_GRADIENT_UPDATE = 0x5A,
    UMESH_CMD_POWER_BEACON    = 0x5B,
    UMESH_CMD_USER_BASE     = 0xE0,
    UMESH_CMD_RAW           = 0xFF,
} umesh_cmd_t;

typedef struct {
    uint8_t  node_id;
    uint8_t  distance;
    int8_t   rssi;
    uint32_t last_seen_ms;
} umesh_neighbor_t;

typedef struct {
    uint32_t sleep_count;
    uint32_t total_sleep_ms;
    uint32_t total_active_ms;
    float    duty_cycle_pct;
    float    estimated_ma;
} umesh_power_stats_t;

/* ── Internal frame ────────────────────────── */
typedef struct {
    uint8_t  net_id;
    uint8_t  dst;
    uint8_t  src;
    uint8_t  flags;
    uint8_t  cmd;
    uint8_t  payload_len;
    uint16_t seq_num;
    uint8_t  hop_count;
    uint8_t  payload[UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE];
    uint16_t crc;
} umesh_frame_t;

/* ── Utility macros ────────────────────────── */
#define UMESH_ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define UMESH_UNUSED(x)      (void)(x)
#define UMESH_MIN(a,b)       ((a) < (b) ? (a) : (b))
#define UMESH_MAX(a,b)       ((a) > (b) ? (a) : (b))

#define UMESH_HOP_FROM_SEQ(s)  (((s) >> 12) & 0x0F)
#define UMESH_SEQ_FROM_SEQ(s)  ((s) & 0x0FFF)

/* ── Logging macros (no-op by default) ─────── */
#ifndef UMESH_LOG_DEBUG
#define UMESH_LOG_DEBUG(fmt, ...) (void)0
#endif
#ifndef UMESH_LOG_WARN
#define UMESH_LOG_WARN(fmt, ...)  (void)0
#endif
#ifndef UMESH_LOG_ERROR
#define UMESH_LOG_ERROR(fmt, ...) (void)0
#endif

#endif /* UMESH_DEFS_H */
