#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/context.h"
#include "../src/mac/cca.h"
#include "../src/mac/frame.h"
#include "../src/mac/mac.h"
#include "../src/sec/sec.h"
#include "../port/posix/phy_posix.h"

static int s_pass = 0;
static int s_fail = 0;
static umesh_ctx_t s_ctx;

static umesh_frame_t s_last_rx_frame;
static int s_rx_count = 0;
static int s_ack_seen_count = 0;

static enum {
    HOOK_NONE = 0,
    HOOK_SYNC_ACK,
    HOOK_DELAY_ACK,
    HOOK_DELAY_DUP_ACK,
    HOOK_DELAY_WRONG_SENDER,
    HOOK_DELAY_WRONG_DEST,
    HOOK_DELAY_WRONG_IDENTITY,
    HOOK_DELAY_BAD_MIC,
    HOOK_DELAY_STALE_ACK,
    HOOK_BACKOFF_1,
    HOOK_BACKOFF_2,
} s_hook_mode = HOOK_NONE;

static uint32_t s_delay_samples[4];
static size_t s_delay_sample_count = 0;
static bool s_sync_ack_injected = false;
static umesh_frame_t s_expected_req;

#define TEST_ASSERT(cond, name) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", name); \
            s_pass++; \
        } else { \
            printf("  FAIL: %s  (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

static const uint8_t TEST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static void clear_observed(void)
{
    memset(&s_last_rx_frame, 0, sizeof(s_last_rx_frame));
    s_rx_count = 0;
    s_ack_seen_count = 0;
    s_sync_ack_injected = false;
    s_delay_sample_count = 0;
    s_hook_mode = HOOK_NONE;
    memset(&s_expected_req, 0, sizeof(s_expected_req));
    phy_posix_set_delay_hook(NULL);
}

static void init_ctx(uint8_t node_id, umesh_security_t security, uint32_t epoch)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    umesh_bind_ctx(&s_ctx);
    s_ctx.cfg.net_id = 0x01;
    s_ctx.cfg.channel = 6;
    s_ctx.cfg.tx_power = 60;
    s_ctx.cfg.security = security;
    s_ctx.cfg.security_epoch = epoch;
    sec_init(TEST_KEY, 0x01, security);
    mac_init(node_id);
    mac_set_rx_callback(NULL);
    phy_posix_set_loopback(false);
    clear_observed();
}

static void make_request(umesh_frame_t *frame, uint16_t seq_num, uint8_t cmd)
{
    memset(frame, 0, sizeof(*frame));
    frame->wire_version = UMESH_WIRE_VERSION;
    frame->net_id = 0x01;
    frame->src = 0x02;
    frame->dst = 0x03;
    frame->link_src = 0x02;
    frame->link_dst = 0x03;
    frame->seq_num = seq_num;
    frame->hop_count = 1;
    frame->cmd = cmd;
    frame->flags = UMESH_FLAG_ACK_REQ | UMESH_FLAG_PRIO_NORMAL;
    frame->payload_len = 0;
}

static umesh_result_t serialize_frame(const umesh_frame_t *frame, uint8_t *buf, size_t *len)
{
    return frame_serialize(frame, buf, UMESH_MAX_FRAME_SIZE, len);
}

static void inject_ack_from_request(const umesh_frame_t *req,
                                    uint8_t src_override,
                                    uint8_t dst_override,
                                    uint8_t cmd_override,
                                    uint16_t seq_override,
                                    bool secure,
                                    bool tamper_mic)
{
    umesh_frame_t ack;
    uint8_t buf[UMESH_MAX_FRAME_SIZE];
    size_t len = 0;

    memset(&ack, 0, sizeof(ack));
    ack.wire_version = UMESH_WIRE_VERSION;
    ack.net_id = req->net_id;
    ack.src = (src_override == 0) ? req->dst : src_override;
    ack.dst = (dst_override == 0) ? req->src : dst_override;
    ack.link_src = ack.src;
    ack.link_dst = ack.dst;
    ack.seq_num = (seq_override == 0) ? req->seq_num : seq_override;
    ack.hop_count = 1;
    ack.cmd = (cmd_override == 0) ? req->cmd : cmd_override;
    ack.flags = (uint8_t)(UMESH_FLAG_IS_ACK | UMESH_FLAG_PRIO_HIGH);
    ack.payload_len = 0;

    if (secure) {
        TEST_ASSERT(sec_encrypt_frame(&ack) == UMESH_OK, "ack: secure encrypt OK");
        if (tamper_mic && ack.payload_len >= UMESH_MIC_SIZE) {
            ack.payload[0] ^= 0xFF;
        }
    }

    if (serialize_frame(&ack, buf, &len) == UMESH_OK) {
        mac_on_raw_rx(buf, (uint8_t)len, -60);
    }
}

static void test_rx_cb(umesh_frame_t *frame, int8_t rssi)
{
    (void)rssi;
    s_last_rx_frame = *frame;
    s_rx_count++;

    if (frame->flags & UMESH_FLAG_IS_ACK) {
        s_ack_seen_count++;
        return;
    }

    if (s_hook_mode == HOOK_NONE || s_sync_ack_injected) {
        return;
    }

    if (frame->flags & UMESH_FLAG_ACK_REQ &&
        (s_hook_mode == HOOK_SYNC_ACK || s_hook_mode == HOOK_DELAY_ACK ||
         s_hook_mode == HOOK_DELAY_DUP_ACK || s_hook_mode == HOOK_DELAY_WRONG_SENDER ||
         s_hook_mode == HOOK_DELAY_WRONG_DEST || s_hook_mode == HOOK_DELAY_WRONG_IDENTITY ||
         s_hook_mode == HOOK_DELAY_BAD_MIC || s_hook_mode == HOOK_DELAY_STALE_ACK)) {
        s_sync_ack_injected = true;
        inject_ack_from_request(frame, 0, 0, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, false);
    }
}

static void delay_hook_common(uint32_t duration_ms)
{
    s_delay_samples[s_delay_sample_count++] = duration_ms;
    if (s_hook_mode == HOOK_DELAY_ACK) {
        inject_ack_from_request(&s_expected_req, 0, 0, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, false);
    } else if (s_hook_mode == HOOK_DELAY_DUP_ACK) {
        inject_ack_from_request(&s_expected_req, 0, 0, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, false);
        inject_ack_from_request(&s_expected_req, 0, 0, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, false);
    } else if (s_hook_mode == HOOK_DELAY_WRONG_SENDER) {
        inject_ack_from_request(&s_expected_req, 0x09, 0, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, false);
    } else if (s_hook_mode == HOOK_DELAY_WRONG_DEST) {
        inject_ack_from_request(&s_expected_req, 0, 0x0A, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, false);
    } else if (s_hook_mode == HOOK_DELAY_WRONG_IDENTITY) {
        inject_ack_from_request(&s_expected_req, 0, 0, UMESH_CMD_SENSOR_TEMP, 0x2222,
                                s_ctx.cfg.security != UMESH_SEC_NONE, false);
    } else if (s_hook_mode == HOOK_DELAY_BAD_MIC) {
        inject_ack_from_request(&s_expected_req, 0, 0, 0, 0, s_ctx.cfg.security != UMESH_SEC_NONE, true);
    } else if (s_hook_mode == HOOK_DELAY_STALE_ACK) {
        inject_ack_from_request(&s_expected_req, 0, 0, 0, (uint16_t)(s_expected_req.seq_num - 1u),
                                s_ctx.cfg.security != UMESH_SEC_NONE, false);
    }
}

static umesh_result_t send_request(uint16_t seq_num, uint8_t cmd)
{
    umesh_frame_t req;
    make_request(&req, seq_num, cmd);
    s_expected_req = req;
    return mac_send(&req);
}

static void test_sync_ack_during_send(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_NONE, 0);
    phy_posix_set_loopback(true);
    mac_set_rx_callback(test_rx_cb);
    s_hook_mode = HOOK_SYNC_ACK;

    r = send_request(0x0101, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_OK, "sync-ack: send OK");
    TEST_ASSERT(mac_get_stats().ack_count == 1, "sync-ack: received during send");
}

static void test_delayed_ack(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x1000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_ACK;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0202, UMESH_CMD_SENSOR_TEMP);
    TEST_ASSERT(r == UMESH_OK, "delayed-ack: send OK");
    TEST_ASSERT(mac_get_stats().ack_count == 1, "delayed-ack: received");
}

static void test_lost_ack_retry_count(void)
{
    umesh_result_t r;
    mac_stats_t stats;

    init_ctx(0x02, UMESH_SEC_NONE, 0);
    mac_set_rx_callback(NULL);
    phy_posix_set_loopback(false);

    r = send_request(0x0303, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_ERR_NO_ACK, "lost-ack: send failed");
    stats = mac_get_stats();
    TEST_ASSERT(stats.retry_count == UMESH_MAX_RETRIES, "lost-ack: retry count");
}

static void test_duplicate_ack(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x2000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_DUP_ACK;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0404, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_OK, "duplicate-ack: send OK");
    TEST_ASSERT(mac_get_stats().ack_count == 1, "duplicate-ack: only one accepted");
}

static void test_stale_ack(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x3000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_STALE_ACK;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0505, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_ERR_NO_ACK, "stale-ack: rejected");
}

static void test_wrong_sender_ack(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x4000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_WRONG_SENDER;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0606, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_ERR_NO_ACK, "wrong-sender: rejected");
}

static void test_wrong_destination_ack(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x5000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_WRONG_DEST;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0707, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_ERR_NO_ACK, "wrong-dest: rejected");
}

static void test_wrong_identity_ack(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x6000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_WRONG_IDENTITY;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0808, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_ERR_NO_ACK, "wrong-identity: rejected");
}

static void test_bad_authenticated_ack_rejected(void)
{
    umesh_result_t r;

    init_ctx(0x02, UMESH_SEC_FULL, 0x7000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(false);
    s_hook_mode = HOOK_DELAY_BAD_MIC;
    phy_posix_set_delay_hook(delay_hook_common);

    r = send_request(0x0909, UMESH_CMD_PING);
    TEST_ASSERT(r == UMESH_ERR_NO_ACK, "bad-auth-ack: rejected");
}

static void test_invalid_protected_frame_gets_no_success_ack(void)
{
    umesh_frame_t frame;
    uint8_t buf[UMESH_MAX_FRAME_SIZE];
    size_t len = 0;

    init_ctx(0x02, UMESH_SEC_FULL, 0x8000);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(true);

    make_request(&frame, 0x0A0A, UMESH_CMD_PING);
    frame.payload_len = 2;
    frame.payload[0] = 0x11;
    frame.payload[1] = 0x22;
    TEST_ASSERT(sec_encrypt_frame(&frame) == UMESH_OK, "invalid-protected: encrypt OK");
    frame.payload[0] ^= 0xFF;
    TEST_ASSERT(frame_serialize(&frame, buf, sizeof(buf), &len) == UMESH_OK,
                "invalid-protected: serialize OK");
    mac_on_raw_rx(buf, (uint8_t)len, -60);
    TEST_ASSERT(mac_get_stats().ack_count == 0, "invalid-protected: no ACK");
}

static void test_broadcast_does_not_create_ack_responses(void)
{
    umesh_result_t r;
    umesh_frame_t frame;

    init_ctx(0x02, UMESH_SEC_NONE, 0);
    mac_set_rx_callback(test_rx_cb);
    phy_posix_set_loopback(true);

    memset(&frame, 0, sizeof(frame));
    frame.wire_version = UMESH_WIRE_VERSION;
    frame.net_id = 0x01;
    frame.src = 0x02;
    frame.dst = UMESH_ADDR_BROADCAST;
    frame.link_src = 0x02;
    frame.link_dst = UMESH_ADDR_BROADCAST;
    frame.seq_num = 0x0B0B;
    frame.hop_count = 1;
    frame.cmd = UMESH_CMD_PING;
    frame.flags = UMESH_FLAG_ACK_REQ | UMESH_FLAG_PRIO_NORMAL;
    frame.payload_len = 0;

    r = mac_send(&frame);
    TEST_ASSERT(r == UMESH_OK, "broadcast: send OK");
    TEST_ASSERT(mac_get_stats().ack_count == 0, "broadcast: no ACKs generated");
}

static void test_two_contexts_have_independent_backoff_state(void)
{
    umesh_ctx_t ctx_a;
    umesh_ctx_t ctx_b;
    uint32_t state_a;
    uint32_t state_b;

    memset(&ctx_a, 0, sizeof(ctx_a));
    memset(&ctx_b, 0, sizeof(ctx_b));

    umesh_bind_ctx(&ctx_a);
    ctx_a.cfg.net_id = 0x01;
    ctx_a.cfg.security = UMESH_SEC_NONE;
    sec_init(TEST_KEY, 0x01, UMESH_SEC_NONE);
    mac_init(0x11);

    umesh_bind_ctx(&ctx_b);
    ctx_b.cfg.net_id = 0x01;
    ctx_b.cfg.security = UMESH_SEC_NONE;
    sec_init(TEST_KEY, 0x01, UMESH_SEC_NONE);
    mac_init(0x11);

    state_a = ctx_a.mac.prng_state;
    state_b = ctx_b.mac.prng_state;

    TEST_ASSERT(state_a != state_b, "backoff: independent state");
}

int main(void)
{
    printf("=== test_mac ===\n");
    test_sync_ack_during_send();
    test_delayed_ack();
    test_lost_ack_retry_count();
    test_duplicate_ack();
    test_stale_ack();
    test_wrong_sender_ack();
    test_wrong_destination_ack();
    test_wrong_identity_ack();
    test_bad_authenticated_ack_rejected();
    test_invalid_protected_frame_gets_no_success_ack();
    test_broadcast_does_not_create_ack_responses();
    test_two_contexts_have_independent_backoff_state();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
