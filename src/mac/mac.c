#include "mac.h"
#include "cca.h"
#include "frame.h"
#include "../phy/phy.h"
#include <stdlib.h>
#include <string.h>

/*
 * MAC layer — CSMA/CA with binary exponential backoff, ACK, retransmit.
 *
 * Timing (POSIX): sleep() not available in ISR context; for POSIX we use
 * a counter-based busy-wait simulation. On ESP32, use vTaskDelay().
 * For testability the actual waiting is abstracted through mac_delay_ms().
 */

/* ── Platform timing ───────────────────────────────────── */
#ifdef _WIN32
#include <windows.h>
static void mac_delay_ms(uint32_t ms)
{
    Sleep(ms);
}
#else
#include <unistd.h>
static void mac_delay_ms(uint32_t ms)
{
    usleep(ms * 1000u);
}
#endif

/* ── State ─────────────────────────────────────────────── */

static void (*s_rx_cb)(umesh_frame_t *frame, int8_t rssi) = NULL;
static mac_stats_t s_stats;
static uint8_t     s_node_id = 0;

/* Pending ACK tracking */
static bool    s_waiting_ack   = false;
static uint8_t s_ack_src       = 0;
static uint16_t s_ack_seq      = 0;
static bool    s_ack_received  = false;

/* ── Helpers ───────────────────────────────────────────── */

/*
 * Compute backoff slots for retry attempt n (0-indexed).
 * Window = 2^(n+2) - 1
 * Slots = rand() % window
 */
static uint8_t backoff_slots(uint8_t retry)
{
    uint8_t window = (uint8_t)((1u << (retry + 2u)) - 1u);
    return (uint8_t)(rand() % (window + 1));
}

/*
 * Send a raw byte buffer through PHY.
 * Performs CCA + backoff before transmitting.
 * Returns UMESH_OK on successful TX, UMESH_ERR_CHANNEL_BUSY if
 * channel stays busy beyond max retries.
 */
static umesh_result_t phy_send_with_cca(const uint8_t *buf, uint8_t len)
{
    uint8_t attempt;

    for (attempt = 0; attempt <= UMESH_MAX_RETRIES; attempt++) {
        if (cca_channel_free()) {
            return phy_send(buf, len);
        }
        /* Channel busy — back off */
        {
            uint8_t slots = backoff_slots(attempt);
            mac_delay_ms((uint32_t)slots * UMESH_SLOT_TIME_MS);
        }
    }
    return UMESH_ERR_CHANNEL_BUSY;
}

/* ── PHY RX callback ───────────────────────────────────── */

static void on_phy_rx(const uint8_t *buf, uint8_t len, int8_t rssi)
{
    umesh_frame_t frame;
    umesh_result_t r;

    cca_set_rssi(rssi);
    cca_set_rx_in_progress(true);

    r = frame_deserialize(buf, len, &frame);
    if (r != UMESH_OK) {
        s_stats.drop_count++;
        cca_set_rx_in_progress(false);
        return;
    }

    s_stats.rx_count++;

    /* Check for ACK directed at us */
    if ((frame.flags & UMESH_FLAG_IS_ACK) &&
        s_waiting_ack &&
        frame.src == s_ack_src &&
        frame.seq_num == s_ack_seq) {
        s_ack_received = true;
        s_stats.ack_count++;
        cca_set_rx_in_progress(false);
        return;
    }

    /* If frame requests ACK and is directed to us, send ACK */
    if ((frame.flags & UMESH_FLAG_ACK_REQ) &&
        (frame.dst == s_node_id || frame.dst == UMESH_ADDR_BROADCAST)) {
        umesh_frame_t ack;
        uint8_t ack_buf[UMESH_FRAME_HEADER_SIZE + 2];
        uint8_t ack_len = 0;

        memset(&ack, 0, sizeof(ack));
        ack.net_id      = frame.net_id;
        ack.dst         = frame.src;
        ack.src         = s_node_id;
        ack.flags       = (uint8_t)(UMESH_FLAG_IS_ACK | UMESH_FLAG_PRIO_HIGH);
        ack.cmd         = frame.cmd;
        ack.seq_num     = frame.seq_num;
        ack.payload_len = 0;

        if (frame_serialize(&ack, ack_buf, sizeof(ack_buf), &ack_len)
            == UMESH_OK) {
            phy_send(ack_buf, ack_len);
        }
    }

    /* Deliver to upper layer */
    if (s_rx_cb) {
        s_rx_cb(&frame, rssi);
    }

    cca_set_rx_in_progress(false);
}

/* ── Public API ────────────────────────────────────────── */

umesh_result_t mac_init(uint8_t node_id)
{
    s_node_id = node_id;
    cca_init();
    memset(&s_stats, 0, sizeof(s_stats));
    s_waiting_ack  = false;
    s_ack_received = false;

    /* Seed PRNG differently per node (best effort on POSIX) */
    srand((unsigned int)node_id + 1u);

    phy_set_rx_cb(on_phy_rx);
    return UMESH_OK;
}

umesh_result_t mac_send(umesh_frame_t *frame)
{
    uint8_t  buf[UMESH_MAX_FRAME_SIZE];
    uint8_t  len = 0;
    umesh_result_t r;
    bool     needs_ack;
    uint8_t  retry;

    if (!frame) return UMESH_ERR_NULL_PTR;

    needs_ack = (frame->flags & UMESH_FLAG_ACK_REQ) &&
                (frame->dst != UMESH_ADDR_BROADCAST);

    r = frame_serialize(frame, buf, sizeof(buf), &len);
    if (r != UMESH_OK) return r;

    for (retry = 0; retry <= UMESH_MAX_RETRIES; retry++) {
        if (retry > 0) {
            uint8_t slots = backoff_slots(retry - 1u);
            mac_delay_ms((uint32_t)slots * UMESH_SLOT_TIME_MS);
            s_stats.retry_count++;
        }

        r = phy_send_with_cca(buf, len);
        if (r != UMESH_OK) continue;

        s_stats.tx_count++;

        if (!needs_ack) {
            return UMESH_OK;
        }

        /* Wait for ACK */
        s_waiting_ack  = true;
        s_ack_src      = frame->dst;
        s_ack_seq      = frame->seq_num;
        s_ack_received = false;

        mac_delay_ms(UMESH_ACK_TIMEOUT_MS);

        s_waiting_ack = false;

        if (s_ack_received) {
            return UMESH_OK;
        }
    }

    s_stats.drop_count++;
    return needs_ack ? UMESH_ERR_NO_ACK : UMESH_ERR_MAX_RETRIES;
}

void mac_set_rx_callback(void (*cb)(umesh_frame_t *frame, int8_t rssi))
{
    s_rx_cb = cb;
}

bool mac_channel_is_free(void)
{
    return cca_channel_free();
}

mac_stats_t mac_get_stats(void)
{
    return s_stats;
}

void mac_on_raw_rx(const uint8_t *buf, uint8_t len, int8_t rssi)
{
    on_phy_rx(buf, len, rssi);
}
