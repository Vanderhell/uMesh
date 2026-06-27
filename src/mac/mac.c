#include "mac.h"
#include "cca.h"
#include "frame.h"
#include "../context.h"
#include "../phy/phy.h"
#include "../sec/sec.h"
#include <stdlib.h>
#include <string.h>

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

static uint8_t backoff_slots(uint8_t retry)
{
    uint8_t window = (uint8_t)((1u << (retry + 2u)) - 1u);
    return (uint8_t)(rand() % (window + 1));
}

static umesh_result_t phy_send_with_cca(const uint8_t *buf, uint8_t len)
{
    uint8_t attempt;

    for (attempt = 0; attempt <= UMESH_MAX_RETRIES; attempt++) {
        if (cca_channel_free()) {
            return phy_send(buf, len);
        }
        mac_delay_ms((uint32_t)backoff_slots(attempt) * UMESH_SLOT_TIME_MS);
    }
    return UMESH_ERR_CHANNEL_BUSY;
}

static void on_phy_rx(const uint8_t *buf, uint8_t len, int8_t rssi)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    umesh_frame_t frame;
    umesh_result_t r;

    cca_set_rssi(rssi);
    cca_set_rx_in_progress(true);

    r = frame_deserialize(buf, len, &frame);
    if (r != UMESH_OK) {
        ctx->mac.stats.drop_count++;
        cca_set_rx_in_progress(false);
        return;
    }

    ctx->mac.stats.rx_count++;

    if ((frame.flags & UMESH_FLAG_IS_ACK) &&
        ctx->mac.waiting_ack &&
        frame.src == ctx->mac.ack_src &&
        frame.seq_num == ctx->mac.ack_seq) {
        ctx->mac.ack_received = true;
        ctx->mac.stats.ack_count++;
        cca_set_rx_in_progress(false);
        return;
    }

    if ((frame.flags & UMESH_FLAG_ACK_REQ) &&
        (frame.link_dst == ctx->mac.node_id || frame.link_dst == UMESH_ADDR_BROADCAST)) {
        umesh_frame_t ack;
        uint8_t ack_buf[UMESH_FRAME_MIN_SIZE];
        size_t ack_len = 0;

        memset(&ack, 0, sizeof(ack));
        ack.wire_version = UMESH_WIRE_VERSION;
        ack.net_id      = frame.net_id;
        ack.dst         = frame.link_src;
        ack.src         = ctx->mac.node_id;
        ack.link_src    = ctx->mac.node_id;
        ack.link_dst    = frame.link_src;
        ack.flags       = (uint8_t)(UMESH_FLAG_IS_ACK | UMESH_FLAG_PRIO_HIGH);
        ack.cmd         = frame.cmd;
        ack.seq_num     = frame.seq_num;
        ack.payload_len = 0;

        if (frame_serialize(&ack, ack_buf, sizeof(ack_buf), &ack_len)
            == UMESH_OK) {
            if (ack_len <= UINT8_MAX) {
                phy_send(ack_buf, (uint8_t)ack_len);
            }
        }
    }

    if (ctx->mac.rx_cb) {
        if (!(frame.flags & UMESH_FLAG_IS_ACK)) {
            r = sec_decrypt_frame(&frame);
            if (r != UMESH_OK) {
                ctx->mac.stats.drop_count++;
                cca_set_rx_in_progress(false);
                return;
            }
        }
        ctx->mac.rx_cb(&frame, rssi);
    }

    cca_set_rx_in_progress(false);
}

void mac_set_node_id(uint8_t node_id)
{
    umesh_current_ctx()->mac.node_id = node_id;
}

umesh_result_t mac_init(uint8_t node_id)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->mac.node_id = node_id;
    cca_init();
    memset(&ctx->mac.stats, 0, sizeof(ctx->mac.stats));
    ctx->mac.waiting_ack  = false;
    ctx->mac.ack_received = false;
    ctx->mac.rx_cb = NULL;
    ctx->mac.last_rssi = -100;
    ctx->mac.rx_in_progress = false;
    srand((unsigned int)node_id + 1u);
    phy_set_rx_cb(on_phy_rx);
    return UMESH_OK;
}

umesh_result_t mac_send(umesh_frame_t *frame)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    uint8_t buf[UMESH_MAX_FRAME_SIZE];
    size_t len = 0;
    umesh_result_t r;
    bool needs_ack;
    uint8_t retry;

    if (!frame) return UMESH_ERR_NULL_PTR;
    if (frame->wire_version == 0) {
        frame->wire_version = UMESH_WIRE_VERSION;
    }
    if (frame->src == 0) {
        frame->src = ctx->mac.node_id;
    }
    if (frame->link_src == 0) {
        frame->link_src = ctx->mac.node_id;
    }
    if (frame->link_dst == 0) {
        frame->link_dst = frame->dst;
    }

    needs_ack = (frame->flags & UMESH_FLAG_ACK_REQ) &&
                (frame->link_dst != UMESH_ADDR_BROADCAST);

    if (!(frame->flags & UMESH_FLAG_IS_ACK)) {
        r = sec_encrypt_frame(frame);
        if (r != UMESH_OK) return r;
    }

    r = frame_serialize(frame, buf, sizeof(buf), &len);
    if (r != UMESH_OK) return r;

    for (retry = 0; retry <= UMESH_MAX_RETRIES; retry++) {
        if (retry > 0) {
            mac_delay_ms((uint32_t)backoff_slots(retry - 1u) * UMESH_SLOT_TIME_MS);
            ctx->mac.stats.retry_count++;
        }

        if (len > UINT8_MAX) {
            return UMESH_ERR_TOO_LONG;
        }

        r = phy_send_with_cca(buf, (uint8_t)len);
        if (r != UMESH_OK) continue;

        ctx->mac.stats.tx_count++;

        if (!needs_ack) {
            return UMESH_OK;
        }

        ctx->mac.waiting_ack  = true;
        ctx->mac.ack_src      = frame->link_dst;
        ctx->mac.ack_seq      = frame->seq_num;
        ctx->mac.ack_received = false;

        mac_delay_ms(UMESH_ACK_TIMEOUT_MS);

        ctx->mac.waiting_ack = false;

        if (ctx->mac.ack_received) {
            return UMESH_OK;
        }
    }

    ctx->mac.stats.drop_count++;
    return needs_ack ? UMESH_ERR_NO_ACK : UMESH_ERR_MAX_RETRIES;
}

void mac_set_rx_callback(void (*cb)(umesh_frame_t *frame, int8_t rssi))
{
    umesh_current_ctx()->mac.rx_cb = cb;
}

bool mac_channel_is_free(void)
{
    return cca_channel_free();
}

mac_stats_t mac_get_stats(void)
{
    return umesh_current_ctx()->mac.stats;
}

void mac_on_raw_rx(const uint8_t *buf, uint8_t len, int8_t rssi)
{
    on_phy_rx(buf, len, rssi);
}
