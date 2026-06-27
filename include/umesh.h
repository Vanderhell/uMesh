#ifndef UMESH_H
#define UMESH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "umesh_caps.h"

#ifndef UMESH_ENABLE_POWER_MANAGEMENT
#define UMESH_ENABLE_POWER_MANAGEMENT 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UMESH_ROLE_COORDINATOR = 0,
    UMESH_ROLE_ROUTER      = 1,
    UMESH_ROLE_END_NODE    = 2,
    UMESH_ROLE_AUTO        = 3,
} umesh_role_t;

typedef enum {
    UMESH_STATE_UNINIT       = 0,
    UMESH_STATE_SCANNING     = 1,
    UMESH_STATE_ELECTION     = 2,
    UMESH_STATE_JOINING      = 3,
    UMESH_STATE_CONNECTED    = 4,
    UMESH_STATE_DISCONNECTED = 5,
} umesh_state_t;

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

#define UMESH_ADDR_BROADCAST   0x00
#define UMESH_ADDR_COORDINATOR 0x01
#define UMESH_ADDR_UNASSIGNED  0xFE

#define UMESH_DEFAULT_CHANNEL   6

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
#define UMESH_MAX_FRAME_SIZE   256

#define UMESH_SLOT_TIME_MS      1
#define UMESH_CCA_TIME_MS       2
#define UMESH_ACK_TIMEOUT_MS   50
#define UMESH_MAX_RETRIES       4
#define UMESH_CCA_THRESHOLD   -85

#define UMESH_KEY_SIZE         16
#define UMESH_MIC_SIZE          4
#define UMESH_NONCE_SIZE       16

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

#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX     78
#endif
#define UMESH_TX_POWER_DEFAULT 60
#define UMESH_TX_POWER_LOW     20

#define UMESH_RSSI_EXCELLENT   -50
#define UMESH_RSSI_GOOD        -70
#define UMESH_RSSI_FAIR        -85
#define UMESH_RSSI_POOR        -95

#define UMESH_FLAG_ACK_REQ     (1 << 0)
#define UMESH_FLAG_IS_ACK      (1 << 1)
#define UMESH_FLAG_ENCRYPTED   (1 << 2)
#define UMESH_FLAG_PRIO_MASK   (3 << 6)
#define UMESH_FLAG_PRIO_HIGH   (3 << 6)
#define UMESH_FLAG_PRIO_NORMAL (2 << 6)
#define UMESH_FLAG_PRIO_LOW    (1 << 6)
#define UMESH_FLAG_PRIO_BULK   (0 << 6)
#define UMESH_FLAG_VALID_MASK  (UMESH_FLAG_ACK_REQ | \
                                UMESH_FLAG_IS_ACK | \
                                UMESH_FLAG_ENCRYPTED | \
                                UMESH_FLAG_PRIO_MASK)

#define UMESH_WIRE_VERSION      1

#define UMESH_CAP_WIFI      (1u << 0)
#define UMESH_CAP_BT        (1u << 1)
#define UMESH_CAP_TWT       (1u << 2)
#define UMESH_CAP_POWER_MGT (1u << 3)

typedef enum {
    UMESH_CMD_PING            = 0x01,
    UMESH_CMD_PONG            = 0x02,
    UMESH_CMD_RESET           = 0x03,
    UMESH_CMD_STATUS          = 0x04,
    UMESH_CMD_SENSOR_TEMP     = 0x10,
    UMESH_CMD_SENSOR_HUM      = 0x11,
    UMESH_CMD_SENSOR_PRESS    = 0x12,
    UMESH_CMD_SENSOR_RAW      = 0x13,
    UMESH_CMD_SET_INTERVAL    = 0x30,
    UMESH_CMD_SET_THRESHOLD   = 0x31,
    UMESH_CMD_SET_MODE        = 0x32,
    UMESH_CMD_JOIN            = 0x50,
    UMESH_CMD_ASSIGN          = 0x51,
    UMESH_CMD_LEAVE           = 0x52,
    UMESH_CMD_DISCOVER        = 0x53,
    UMESH_CMD_ROUTE_UPDATE    = 0x54,
    UMESH_CMD_NODE_JOINED     = 0x55,
    UMESH_CMD_NODE_LEFT       = 0x56,
    UMESH_CMD_ELECTION        = 0x57,
    UMESH_CMD_ELECTION_RESULT = 0x58,
    UMESH_CMD_GRADIENT_BEACON = 0x59,
    UMESH_CMD_GRADIENT_UPDATE = 0x5A,
    UMESH_CMD_POWER_BEACON    = 0x5B,
    UMESH_CMD_USER_BASE       = 0xE0,
    UMESH_CMD_RAW             = 0xFF,
} umesh_cmd_t;

typedef enum {
    UMESH_OK                =  0,
    UMESH_ERR_NO_ACK        =  1,
    UMESH_ERR_CHANNEL_BUSY  =  2,
    UMESH_ERR_MAX_RETRIES   =  3,
    UMESH_ERR_NOT_ROUTABLE  =  4,
    UMESH_ERR_NOT_JOINED    =  5,
    UMESH_ERR_TIMEOUT       =  6,
    UMESH_ERR_INVALID_DST   =  7,
    UMESH_ERR_TOO_LONG      =  8,
    UMESH_ERR_NULL_PTR      =  9,
    UMESH_ERR_NOT_INIT      = 10,
    UMESH_ERR_HARDWARE      = 11,
    UMESH_ERR_CRC_FAIL      = 12,
    UMESH_ERR_MIC_FAIL      = 13,
    UMESH_ERR_REPLAY        = 14,
    UMESH_ERR_NOT_SUPPORTED = 15,
} umesh_result_t;

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

typedef struct {
    uint8_t  dst_node;
    uint8_t  next_hop;
    uint8_t  hop_count;
    int8_t   last_rssi;
    uint8_t  metric;
    uint32_t last_seen_ms;
    bool     valid;
} umesh_route_entry_t;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t ack_count;
    uint32_t retry_count;
    uint32_t drop_count;
    uint32_t mic_fail_count;
    uint32_t replay_count;
} umesh_stats_t;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t ack_count;
    uint32_t retry_count;
    uint32_t drop_count;
} mac_stats_t;

typedef struct umesh_frame_t {
    uint8_t  wire_version;
    uint8_t  net_id;
    uint8_t  src;
    uint8_t  dst;
    uint8_t  link_src;
    uint8_t  link_dst;
    uint16_t seq_num;
    uint8_t  hop_count;
    uint8_t  cmd;
    uint8_t  flags;
    uint16_t payload_len;
    uint8_t  payload[UMESH_MAX_PAYLOAD + UMESH_MIC_SIZE];
    uint16_t crc;
} umesh_frame_t;

typedef struct {
    uint8_t          net_id;
    uint8_t          node_id;
    const uint8_t   *master_key;
    umesh_role_t     role;
    umesh_security_t security;
    uint8_t          channel;
    uint8_t          tx_power;
    umesh_routing_mode_t routing;
    umesh_power_mode_t power_mode;
    uint32_t         gradient_beacon_ms;
    uint32_t         gradient_jitter_max_ms;
    uint32_t         light_sleep_interval_ms;
    uint32_t         light_listen_window_ms;
    uint32_t         deep_sleep_tx_interval_ms;
    uint32_t         scan_ms;
    uint32_t         election_ms;
    void           (*on_joined)(uint8_t node_id);
    void           (*on_role_elected)(umesh_role_t role);
    void           (*on_node_joined)(uint8_t node_id);
    void           (*on_node_left)(uint8_t node_id);
    void           (*on_gradient_ready)(uint8_t distance);
    void           (*on_sleep)(void);
    void           (*on_wake)(void);
    void           (*on_error)(umesh_result_t err);
} umesh_cfg_t;

typedef struct {
    uint8_t       node_id;
    uint8_t       net_id;
    umesh_role_t  role;
    umesh_state_t state;
    uint8_t       coordinator;
    uint8_t       node_count;
    uint8_t       channel;
    int8_t        rssi;
    const char   *target;
    uint8_t       wifi_gen;
    uint8_t       tx_power_max;
} umesh_info_t;

typedef struct {
    uint8_t  src;
    uint8_t  dst;
    uint8_t  cmd;
    uint8_t *payload;
    uint8_t  payload_len;
    int8_t   rssi;
} umesh_pkt_t;

typedef struct {
    uint8_t  src_node;
    uint16_t last_seq;
    uint32_t window_bitmap;
    bool     valid;
} umesh_replay_state_t;

typedef struct {
    uint8_t  channel;
    uint8_t  tx_power;
    uint8_t  net_id;
} umesh_phy_cfg_t;

typedef struct umesh_ctx_t {
    bool initialized;
    umesh_cfg_t cfg;
    umesh_stats_t stats;
    void (*rx_cb)(umesh_pkt_t *pkt);
    void (*cmd_cb[256])(umesh_pkt_t *pkt);
    bool role_notified;
    bool join_notified;
    bool gradient_notified;
    umesh_role_t last_role;
    uint8_t last_node_id;
    uint8_t last_gradient_distance;
    uint8_t pending_local_mac[6];

    struct {
        uint8_t node_id;
        uint8_t net_id;
        umesh_role_t role;
        umesh_role_t role_cfg;
        umesh_state_t state;
        uint16_t seq_num;
        uint32_t last_route_update_ms;
        uint32_t last_coord_seen_ms;
        uint32_t last_join_ms;
        uint32_t state_enter_ms;
        uint32_t now_ms;
        uint32_t scan_ms;
        uint32_t election_ms;
        umesh_routing_mode_t routing_mode;
        uint32_t gradient_beacon_ms;
        uint32_t gradient_jitter_max_ms;
        uint32_t last_gradient_beacon_ms;
        uint32_t last_election_result_ms;
#if UMESH_ENABLE_POWER_MANAGEMENT
        umesh_power_mode_t power_mode;
        uint32_t light_sleep_interval_ms;
        uint32_t light_listen_window_ms;
        uint32_t last_power_beacon_ms;
#endif
        void (*rx_cb)(const umesh_frame_t *frame, int8_t rssi);
    } net;

    struct {
        uint8_t net_id;
        uint8_t node_id;
        umesh_role_t role;
        uint16_t seq_num;
        uint8_t assigned_id;
        bool joined;
        uint8_t next_assign_id;
        uint32_t scan_ms;
        uint32_t election_ms;
        uint8_t local_mac[6];
        uint32_t now_ms;
        bool auto_seen_coordinator;
        bool auto_saw_lower_mac;
        bool auto_seen_result;
        uint8_t auto_winner_mac[6];
        bool gradient_enabled;
        uint8_t gradient_distance;
        bool gradient_ready;
        bool gradient_rebroadcast_pending;
        uint32_t gradient_rebroadcast_at_ms;
        uint32_t gradient_jitter_max_ms;
    } discovery;

    struct {
        umesh_route_entry_t table[UMESH_MAX_ROUTES];
        umesh_neighbor_t neighbors[UMESH_MAX_NEIGHBORS];
    } routing;

    struct {
        void (*rx_cb)(umesh_frame_t *frame, int8_t rssi);
        mac_stats_t stats;
        uint8_t node_id;
        bool waiting_ack;
        uint8_t ack_src;
        uint16_t ack_seq;
        bool ack_received;
        int8_t last_rssi;
        bool rx_in_progress;
    } mac;

    struct {
        umesh_security_t level;
        uint8_t enc_key[UMESH_KEY_SIZE];
        uint8_t auth_key[UMESH_KEY_SIZE];
        uint8_t salt[3];
        uint8_t net_id;
        umesh_replay_state_t replay[UMESH_MAX_NODES];
    } sec;

#if UMESH_ENABLE_POWER_MANAGEMENT
    struct {
        umesh_power_mode_t mode;
        uint32_t light_interval_ms;
        uint32_t light_window_ms;
        uint32_t deep_tx_interval_ms;
        uint32_t last_tick_ms;
        bool initialized;
        bool in_sleep_phase;
        void (*on_sleep)(void);
        void (*on_wake)(void);
        umesh_power_stats_t stats;
    } power;
#endif
} umesh_ctx_t;

void umesh_bind_ctx(umesh_ctx_t *ctx);
umesh_ctx_t *umesh_current_ctx(void);

umesh_result_t umesh_init_ctx(umesh_ctx_t *ctx, const umesh_cfg_t *cfg);
umesh_result_t umesh_start_ctx(umesh_ctx_t *ctx);
umesh_result_t umesh_stop_ctx(umesh_ctx_t *ctx);
umesh_result_t umesh_reset_ctx(umesh_ctx_t *ctx);
umesh_result_t umesh_send_ctx(umesh_ctx_t *ctx, uint8_t dst, uint8_t cmd,
                              const void *payload, uint8_t len,
                              uint8_t flags);
umesh_result_t umesh_send_cmd_ctx(umesh_ctx_t *ctx, uint8_t dst, uint8_t cmd,
                                  uint8_t flags);
umesh_result_t umesh_broadcast_ctx(umesh_ctx_t *ctx, uint8_t cmd,
                                   const void *payload, uint8_t len);
umesh_result_t umesh_send_raw_ctx(umesh_ctx_t *ctx, uint8_t dst,
                                  const void *payload, uint8_t len,
                                  uint8_t flags);
void umesh_on_receive_ctx(umesh_ctx_t *ctx, void (*cb)(umesh_pkt_t *pkt));
void umesh_on_cmd_ctx(umesh_ctx_t *ctx, uint8_t cmd, void (*cb)(umesh_pkt_t *pkt));
umesh_info_t umesh_get_info_ctx(umesh_ctx_t *ctx);
const char *umesh_get_target(void);
uint8_t umesh_get_wifi_gen(void);
bool umesh_target_supports(uint32_t capability);
umesh_role_t umesh_get_role_ctx(umesh_ctx_t *ctx);
bool umesh_is_coordinator_ctx(umesh_ctx_t *ctx);
umesh_result_t umesh_trigger_election_ctx(umesh_ctx_t *ctx);
uint8_t umesh_gradient_distance_ctx(umesh_ctx_t *ctx);
umesh_routing_mode_t umesh_get_routing_mode_ctx(umesh_ctx_t *ctx);
umesh_result_t umesh_gradient_refresh_ctx(umesh_ctx_t *ctx);
uint8_t umesh_get_neighbor_count_ctx(umesh_ctx_t *ctx);
umesh_neighbor_t umesh_get_neighbor_ctx(umesh_ctx_t *ctx, uint8_t index);
umesh_result_t umesh_set_power_mode_ctx(umesh_ctx_t *ctx, umesh_power_mode_t mode);
umesh_power_mode_t umesh_get_power_mode_ctx(umesh_ctx_t *ctx);
umesh_result_t umesh_deep_sleep_cycle_ctx(umesh_ctx_t *ctx);
float umesh_estimate_current_ma_ctx(umesh_ctx_t *ctx);
umesh_power_stats_t umesh_get_power_stats_ctx(umesh_ctx_t *ctx);
umesh_stats_t umesh_get_stats_ctx(umesh_ctx_t *ctx);
void umesh_tick_ctx(umesh_ctx_t *ctx, uint32_t now_ms);
const char *umesh_err_str(umesh_result_t err);

umesh_result_t umesh_init(const umesh_cfg_t *cfg);
umesh_result_t umesh_start(void);
umesh_result_t umesh_stop(void);
umesh_result_t umesh_reset(void);
umesh_result_t umesh_send(uint8_t dst, uint8_t cmd,
                          const void *payload, uint8_t len,
                          uint8_t flags);
umesh_result_t umesh_send_cmd(uint8_t dst, uint8_t cmd,
                              uint8_t flags);
umesh_result_t umesh_broadcast(uint8_t cmd,
                               const void *payload, uint8_t len);
umesh_result_t umesh_send_raw(uint8_t dst,
                              const void *payload, uint8_t len,
                              uint8_t flags);
void umesh_on_receive(void (*cb)(umesh_pkt_t *pkt));
void umesh_on_cmd(uint8_t cmd, void (*cb)(umesh_pkt_t *pkt));
umesh_info_t umesh_get_info(void);
umesh_role_t umesh_get_role(void);
bool umesh_is_coordinator(void);
umesh_result_t umesh_trigger_election(void);
uint8_t umesh_gradient_distance(void);
umesh_routing_mode_t umesh_get_routing_mode(void);
umesh_result_t umesh_gradient_refresh(void);
uint8_t umesh_get_neighbor_count(void);
umesh_neighbor_t umesh_get_neighbor(uint8_t index);
umesh_result_t umesh_set_power_mode(umesh_power_mode_t mode);
umesh_power_mode_t umesh_get_power_mode(void);
umesh_result_t umesh_deep_sleep_cycle(void);
float umesh_estimate_current_ma(void);
umesh_power_stats_t umesh_get_power_stats(void);
umesh_stats_t umesh_get_stats(void);
void umesh_tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* UMESH_H */
