#include "mac.h"
#include "cca.h"
#include "frame.h"
#include "../context.h"
#include "../phy/phy.h"
#include "../phy/phy_hal.h"
#include "../sec/sec.h"
#include <string.h>

static uint32_t mac_prng_next(umesh_ctx_t *ctx)
{
    ctx->mac.prng_state = ctx->mac.prng_state * 1664525u + 1013904223u;
    return ctx->mac.prng_state;
}

static uint8_t backoff_slots(umesh_ctx_t *ctx, uint8_t retry)
{
    uint8_t window = (uint8_t)((1u << (retry + 2u)) - 1u);
    return (uint8_t)(mac_prng_next(ctx) & window);
}

static umesh_result_t phy_send_with_cca(const uint8_t *buf, uint8_t len)
{
    uint8_t attempt;

    for (attempt = 0; attempt <= UMESH_MAX_RETRIES; attempt++) {
        if (cca_channel_free()) {
            return phy_send(buf, len);
        }
        phy_hal_delay_ms((uint32_t)backoff_slots(umesh_current_ctx(), attempt) *
                         UMESH_SLOT_TIME_MS);
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

    if (frame.flags & UMESH_FLAG_IS_ACK) {
        if (ctx->cfg.security != UMESH_SEC_NONE) {
            if ((frame.flags & UMESH_FLAG_ENCRYPTED) == 0u) {
                ctx->mac.stats.drop_count++;
                cca_set_rx_in_progress(false);
                return;
            }
            r = sec_decrypt_frame(&frame);
            if (r != UMESH_OK) {
                ctx->mac.stats.drop_count++;
                if (r == UMESH_ERR_MIC_FAIL) {
                    ctx->stats.mic_fail_count++;
                } else if (r == UMESH_ERR_REPLAY) {
                    ctx->stats.replay_count++;
                }
                cca_set_rx_in_progress(false);
                return;
            }
        }

        if (ctx->mac.waiting_ack && !ctx->mac.ack_received &&
            frame.src == ctx->mac.ack_src &&
            frame.dst == ctx->mac.ack_dst &&
            frame.link_src == ctx->mac.ack_src &&
            frame.link_dst == ctx->mac.node_id &&
            frame.cmd == ctx->mac.ack_cmd &&
            frame.seq_num == ctx->mac.ack_seq &&
            ctx->sec.session_epoch == ctx->mac.ack_epoch) {
            ctx->mac.ack_received = true;
            ctx->mac.stats.ack_count++;
        }
        cca_set_rx_in_progress(false);
        return;
    }

    if ((frame.flags & UMESH_FLAG_ENCRYPTED) != 0u) {
        r = sec_decrypt_frame(&frame);
        if (r != UMESH_OK) {
            ctx->mac.stats.drop_count++;
            if (r == UMESH_ERR_MIC_FAIL) {
                ctx->stats.mic_fail_count++;
            } else if (r == UMESH_ERR_REPLAY) {
                ctx->stats.replay_count++;
            }
            cca_set_rx_in_progress(false);
            return;
        }
    }

    if ((frame.flags & UMESH_FLAG_ACK_REQ) &&
        frame.link_dst == ctx->mac.node_id &&
        frame.dst != UMESH_ADDR_BROADCAST) {
        umesh_frame_t ack;
        uint8_t ack_buf[UMESH_FRAME_MIN_SIZE + UMESH_MIC_SIZE];
        size_t ack_len = 0;

        memset(&ack, 0, sizeof(ack));
        ack.wire_version = UMESH_WIRE_VERSION;
        ack.net_id      = frame.net_id;
        ack.dst         = frame.src;
        ack.src         = ctx->mac.node_id;
        ack.link_src    = ctx->mac.node_id;
        ack.link_dst    = frame.src;
        ack.flags       = (uint8_t)(UMESH_FLAG_IS_ACK | UMESH_FLAG_PRIO_HIGH);
        ack.cmd         = frame.cmd;
        ack.seq_num     = frame.seq_num;
        ack.payload_len = 0;

        if (ctx->cfg.security != UMESH_SEC_NONE) {
            r = sec_encrypt_frame(&ack);
            if (r != UMESH_OK) {
                cca_set_rx_in_progress(false);
                return;
            }
        }

        if (frame_serialize(&ack, ack_buf, sizeof(ack_buf), &ack_len) == UMESH_OK &&
            ack_len <= UINT8_MAX) {
            phy_send(ack_buf, (uint8_t)ack_len);
        }
    }

    if (ctx->mac.rx_cb) {
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
    ctx->mac.prng_state = 0x9E3779B9u ^ ((uint32_t)node_id << 16) ^
                          (uint32_t)(uintptr_t)ctx;
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
    if (frame->dst == UMESH_ADDR_BROADCAST ||
        frame->link_dst == UMESH_ADDR_BROADCAST) {
        frame->flags = (uint8_t)(frame->flags & (uint8_t)~UMESH_FLAG_ACK_REQ);
    }

    needs_ack = (frame->flags & UMESH_FLAG_ACK_REQ) &&
                (frame->link_dst != UMESH_ADDR_BROADCAST);

    if (ctx->cfg.security != UMESH_SEC_NONE) {
        r = sec_encrypt_frame(frame);
        if (r != UMESH_OK) return r;
    }

    r = frame_serialize(frame, buf, sizeof(buf), &len);
    if (r != UMESH_OK) return r;

    for (retry = 0; retry <= UMESH_MAX_RETRIES; retry++) {
        if (retry > 0) {
            phy_hal_delay_ms((uint32_t)backoff_slots(ctx, (uint8_t)(retry - 1u)) *
                             UMESH_SLOT_TIME_MS);
            ctx->mac.stats.retry_count++;
        }

        if (len > UINT8_MAX) {
            return UMESH_ERR_TOO_LONG;
        }

        if (needs_ack) {
            ctx->mac.waiting_ack  = true;
            ctx->mac.ack_received = false;
            ctx->mac.ack_src      = frame->link_dst;
            ctx->mac.ack_dst      = frame->src;
            ctx->mac.ack_cmd      = frame->cmd;
            ctx->mac.ack_seq      = frame->seq_num;
            ctx->mac.ack_epoch    = ctx->sec.session_epoch;
        }

        r = phy_send_with_cca(buf, (uint8_t)len);
        if (r != UMESH_OK) {
            ctx->mac.waiting_ack = false;
            continue;
        }

        ctx->mac.stats.tx_count++;

        if (!needs_ack) {
            return UMESH_OK;
        }

        phy_hal_delay_ms(UMESH_ACK_TIMEOUT_MS);

        ctx->mac.waiting_ack = false;

        if (ctx->mac.ack_received) {
            return UMESH_OK;
        }
    }

    ctx->mac.stats.drop_count++;
    ctx->mac.waiting_ack = false;
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
