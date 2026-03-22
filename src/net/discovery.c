#include "discovery.h"
#include "routing.h"
#include "../mac/mac.h"
#include "../mac/frame.h"
#include <string.h>

/*
 * Discovery protocol:
 *   JOIN:        New node (NODE_ID=0xFE) broadcasts CMD_JOIN
 *   ASSIGN:      Coordinator unicasts CMD_ASSIGN with new NODE_ID
 *   NODE_JOINED: Coordinator broadcasts CMD_NODE_JOINED
 *   LEAVE:       Node broadcasts CMD_LEAVE
 *   NODE_LEFT:   Coordinator broadcasts CMD_NODE_LEFT
 */

/* ── State ─────────────────────────────────────────────── */

static uint8_t       s_net_id   = 0;
static uint8_t       s_node_id  = UMESH_ADDR_UNASSIGNED;
static umesh_role_t  s_role     = UMESH_ROLE_END_NODE;
static uint16_t      s_seq_num  = 0;

/* Assigned NODE_ID (filled by COORDINATOR via CMD_ASSIGN) */
static uint8_t       s_assigned_id = UMESH_ADDR_UNASSIGNED;
static bool          s_joined      = false;

/* Per-node assignment tracking (COORDINATOR only) */
static uint8_t s_next_assign_id = 0x02; /* first assignable ID */

/* ── Helpers ───────────────────────────────────────────── */

static uint16_t next_seq(void)
{
    s_seq_num = (uint16_t)((s_seq_num + 1u) & 0x0FFF);
    if (s_seq_num == 0) {
        s_seq_num = 1;
    }
    return s_seq_num;
}

static umesh_result_t send_frame(uint8_t dst, uint8_t cmd,
                                  const uint8_t *payload, uint8_t plen,
                                  uint8_t flags)
{
    umesh_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.net_id      = s_net_id;
    frame.dst         = dst;
    frame.src         = s_node_id;
    frame.flags       = flags;
    frame.cmd         = cmd;
    frame.seq_num     = next_seq();
    frame.hop_count   = UMESH_MAX_HOP_COUNT;
    frame.payload_len = plen;
    if (plen > 0 && payload) {
        memcpy(frame.payload, payload, plen);
    }
    return mac_send(&frame);
}

/* ── Public API ────────────────────────────────────────── */

umesh_result_t discovery_init(uint8_t net_id, uint8_t node_id,
                               umesh_role_t role)
{
    s_net_id        = net_id;
    s_node_id       = node_id;
    s_role          = role;
    s_assigned_id   = node_id;
    s_joined        = (node_id != UMESH_ADDR_UNASSIGNED) &&
                      (node_id != UMESH_ADDR_COORDINATOR ||
                       role == UMESH_ROLE_COORDINATOR);

    if (role == UMESH_ROLE_COORDINATOR) {
        s_node_id    = UMESH_ADDR_COORDINATOR;
        s_assigned_id = UMESH_ADDR_COORDINATOR;
        s_joined     = true;
        s_next_assign_id = 0x02;
    }
    return UMESH_OK;
}

umesh_result_t discovery_join(void)
{
    if (s_role == UMESH_ROLE_COORDINATOR) {
        s_joined = true;
        return UMESH_OK;
    }

    /* Broadcast CMD_JOIN with NODE_ID = UNASSIGNED */
    return send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_JOIN,
                      NULL, 0, UMESH_FLAG_PRIO_HIGH);
}

void discovery_leave(void)
{
    send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_LEAVE,
               NULL, 0, UMESH_FLAG_PRIO_HIGH);
    s_joined    = false;
    s_node_id   = UMESH_ADDR_UNASSIGNED;
}

bool discovery_is_joined(void)
{
    return s_joined;
}

uint8_t discovery_get_node_id(void)
{
    return s_node_id;
}

void discovery_on_frame(const umesh_frame_t *frame, int8_t rssi)
{
    if (!frame) return;
    if (frame->net_id != s_net_id) return;

    switch (frame->cmd) {

    case UMESH_CMD_JOIN:
        /* Only COORDINATOR processes JOIN */
        if (s_role != UMESH_ROLE_COORDINATOR) break;
        if (frame->src != UMESH_ADDR_UNASSIGNED) break;
        {
            /* Assign a new NODE_ID */
            uint8_t new_id = s_next_assign_id++;
            if (s_next_assign_id > 0xFD) s_next_assign_id = 0x02;

            /* CMD_ASSIGN payload: [new_node_id] */
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_ASSIGN,
                       &new_id, 1, UMESH_FLAG_PRIO_HIGH);

            /* Add route to new node via broadcast source */
            routing_add(new_id, new_id, 1, rssi, 0);

            /* Broadcast NODE_JOINED */
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_JOINED,
                       &new_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_ASSIGN:
        /* End nodes and routers accept ASSIGN */
        if (s_role == UMESH_ROLE_COORDINATOR) break;
        if (s_joined) break;
        if (frame->payload_len < 1) break;
        {
            uint8_t new_id = frame->payload[0];
            s_node_id    = new_id;
            s_assigned_id = new_id;
            s_joined     = true;
            /* Add route to coordinator */
            routing_add(UMESH_ADDR_COORDINATOR,
                        UMESH_ADDR_COORDINATOR, 1, rssi, 0);
        }
        break;

    case UMESH_CMD_LEAVE:
        if (s_role == UMESH_ROLE_COORDINATOR) {
            /* Remove from routing table and broadcast NODE_LEFT */
            uint8_t left_id = frame->src;
            routing_remove(left_id);
            send_frame(UMESH_ADDR_BROADCAST, UMESH_CMD_NODE_LEFT,
                       &left_id, 1, UMESH_FLAG_PRIO_NORMAL);
        }
        break;

    case UMESH_CMD_NODE_JOINED:
        if (frame->payload_len >= 1) {
            routing_add(frame->payload[0],
                        UMESH_ADDR_COORDINATOR, 1, rssi, 0);
        }
        break;

    case UMESH_CMD_NODE_LEFT:
        if (frame->payload_len >= 1) {
            routing_remove(frame->payload[0]);
        }
        break;

    case UMESH_CMD_ROUTE_UPDATE:
        /* Only COORDINATOR and ROUTER process ROUTE_UPDATE */
        if (s_role == UMESH_ROLE_END_NODE) break;
        routing_add(frame->src, frame->src, 1, rssi, 0);
        break;

    default:
        break;
    }
}
