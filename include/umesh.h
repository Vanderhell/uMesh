#ifndef UMESH_H
#define UMESH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Pull in common definitions */
#include "../src/common/defs.h"

/* ── Public types ──────────────────────────── */

typedef struct {
    uint8_t          net_id;
    uint8_t          node_id;
    const uint8_t   *master_key;
    umesh_role_t     role;
    umesh_security_t security;
    uint8_t          channel;
    uint8_t          tx_power;
} umesh_cfg_t;

typedef struct {
    uint8_t  src;
    uint8_t  dst;
    uint8_t  cmd;
    uint8_t *payload;
    uint8_t  payload_len;
    int8_t   rssi;
} umesh_pkt_t;

typedef struct {
    uint8_t       node_id;
    uint8_t       net_id;
    umesh_role_t  role;
    umesh_state_t state;
    uint8_t       node_count;
    uint8_t       channel;
} umesh_info_t;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t ack_count;
    uint32_t retry_count;
    uint32_t drop_count;
    uint32_t mic_fail_count;
    uint32_t replay_count;
} umesh_stats_t;

/* ── Public API ────────────────────────────── */

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

umesh_info_t  umesh_get_info(void);
umesh_stats_t umesh_get_stats(void);
const char   *umesh_err_str(umesh_result_t err);

#endif /* UMESH_H */
