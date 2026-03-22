#include "net.h"
#include "discovery.h"
#include "routing.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#include <string.h>

/*
 * Network layer FSM:
 *   UNINIT -> SCANNING -> JOINING -> CONNECTED -> RECONNECTING -> SCANNING
 *
 * BUGFIX-03: SEQ_NUM wrap-around handler (see docs/KNOWN_ISSUES.md)
 */

/* ── State ─────────────────────────────────────────────── */

static uint8_t       s_node_id    = 0;
static uint8_t       s_net_id     = 0;
static umesh_role_t  s_role       = UMESH_ROLE_END_NODE;
static umesh_state_t s_state      = UMESH_STATE_UNINIT;
static uint16_t      s_seq_num    = 0;
static uint32_t      s_last_route_update_ms = 0;
static uint32_t      s_last_coord_seen_ms   = 0;

/* Upper layer RX callback */
static void (*s_net_rx_cb)(const umesh_frame_t *frame, int8_t rssi) = NULL;

/* ── SEQ_NUM management — BUGFIX-03 ──────────────────────
 * SEQ_NUM is 12 bits (0..4095).
 * At ~50 pkt/s wrap-around occurs in ~82 seconds.
 * On overflow, notify security layer (sec_regenerate_salt).
 */
#include "../sec/sec.h"

static uint16_t next_seq(void)
{
    s_seq_num = (uint16_t)((s_seq_num + 1u) & 0x0FFF);
    if (s_seq_num == 0) {
        /* Wrap-around: regenerate SALT to prevent NONCE reuse */
        sec_regenerate_salt();
        s_seq_num = 1;
    }
    return s_seq_num;
}

/* ── MAC RX callback ───────────────────────────────────── */

static void on_mac_rx(umesh_frame_t *frame, int8_t rssi)
{
    if (!frame) return;
    if (frame->net_id != s_net_id) return;

    /* Update last-seen for coordinator */
    if (frame->src == UMESH_ADDR_COORDINATOR) {
        s_last_coord_seen_ms = 0; /* Reset watchdog — caller provides now_ms */
    }

    /* Discovery protocol */
    discovery_on_frame(frame, rssi);

    /* Update our node_id if discovery assigned one */
    if (s_state == UMESH_STATE_JOINING) {
        uint8_t assigned = discovery_get_node_id();
        if (assigned != UMESH_ADDR_UNASSIGNED) {
            s_node_id = assigned;
            s_state   = UMESH_STATE_CONNECTED;
        }
    }

    /* Deliver to upper layer */
    if (s_net_rx_cb) {
        s_net_rx_cb(frame, rssi);
    }
}

/* ── Public API ────────────────────────────────────────── */

umesh_result_t net_init(uint8_t net_id, uint8_t node_id,
                        umesh_role_t role)
{
    s_net_id  = net_id;
    s_node_id = node_id;
    s_role    = role;
    s_state   = UMESH_STATE_SCANNING;
    s_seq_num = 0;
    s_last_route_update_ms = 0;
    s_last_coord_seen_ms   = 0;

    routing_init();

    mac_set_rx_callback(on_mac_rx);

    return discovery_init(net_id, node_id, role);
}

umesh_result_t net_join(void)
{
    if (s_state == UMESH_STATE_UNINIT) return UMESH_ERR_NOT_INIT;

    if (s_role == UMESH_ROLE_COORDINATOR) {
        s_state   = UMESH_STATE_CONNECTED;
        s_node_id = UMESH_ADDR_COORDINATOR;
        return UMESH_OK;
    }

    s_state = UMESH_STATE_JOINING;
    return discovery_join();
}

void net_leave(void)
{
    discovery_leave();
    s_state   = UMESH_STATE_DISCONNECTED;
    s_node_id = UMESH_ADDR_UNASSIGNED;
}

umesh_result_t net_route(umesh_frame_t *frame)
{
    umesh_route_entry_t route;

    if (!frame) return UMESH_ERR_NULL_PTR;
    if (s_state != UMESH_STATE_CONNECTED) return UMESH_ERR_NOT_JOINED;

    if (frame->dst == UMESH_ADDR_BROADCAST) {
        return mac_send(frame);
    }

    if (!routing_find(frame->dst, &route)) {
        /* No direct route — try via coordinator */
        if (!routing_find(UMESH_ADDR_COORDINATOR, &route)) {
            return UMESH_ERR_NOT_ROUTABLE;
        }
    }

    /* Set next hop */
    frame->hop_count = UMESH_MAX_HOP_COUNT;
    frame->src       = s_node_id;
    frame->seq_num   = next_seq();

    return mac_send(frame);
}

uint8_t net_get_node_id(void)
{
    return s_node_id;
}

uint8_t net_get_state(void)
{
    return (uint8_t)s_state;
}

uint8_t net_get_node_count(void)
{
    uint8_t i, count = 0;
    umesh_route_entry_t dummy;
    for (i = 2; i <= 0xFD; i++) {
        if (routing_find(i, &dummy)) count++;
    }
    return count;
}

void net_set_rx_callback(void (*cb)(const umesh_frame_t *frame, int8_t rssi))
{
    s_net_rx_cb = cb;
}

void net_tick(uint32_t now_ms)
{
    routing_expire(now_ms);

    /* ROUTE_UPDATE — only COORDINATOR and ROUTER, every 30s */
    if (s_state == UMESH_STATE_CONNECTED &&
        s_role != UMESH_ROLE_END_NODE) {
        if (now_ms - s_last_route_update_ms >= UMESH_ROUTE_UPDATE_MS) {
            umesh_frame_t frame;
            memset(&frame, 0, sizeof(frame));
            frame.net_id    = s_net_id;
            frame.dst       = UMESH_ADDR_BROADCAST;
            frame.src       = s_node_id;
            frame.cmd       = UMESH_CMD_ROUTE_UPDATE;
            frame.flags     = UMESH_FLAG_PRIO_NORMAL;
            frame.seq_num   = next_seq();
            frame.hop_count = UMESH_MAX_HOP_COUNT;
            mac_send(&frame);
            s_last_route_update_ms = now_ms;
        }
    }

    /* Coordinator timeout → reconnect */
    if (s_state == UMESH_STATE_CONNECTED &&
        s_role != UMESH_ROLE_COORDINATOR) {
        s_last_coord_seen_ms += 1; /* simplified tick */
        if (s_last_coord_seen_ms > UMESH_NODE_TIMEOUT_MS) {
            s_state = UMESH_STATE_DISCONNECTED;
        }
    }
}

void net_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    on_mac_rx((umesh_frame_t *)frame, rssi);
}
