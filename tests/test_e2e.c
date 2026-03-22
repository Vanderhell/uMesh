#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../include/umesh.h"

static int s_pass = 0;
static int s_fail = 0;

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

static int      s_rx_count  = 0;
static int      s_cmd_count = 0;
static uint8_t  s_last_cmd  = 0;
static uint8_t  s_last_src  = 0;

static void on_rx(umesh_pkt_t *pkt)
{
    s_rx_count++;
    s_last_cmd = pkt->cmd;
    s_last_src = pkt->src;
}

static void on_ping(umesh_pkt_t *pkt)
{
    s_cmd_count++;
    s_last_src = pkt->src;
}

static void test_e2e_init_start(void)
{
    umesh_cfg_t cfg;
    umesh_result_t r;

    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id     = 0x01;
    cfg.node_id    = UMESH_ADDR_COORDINATOR;
    cfg.master_key = TEST_KEY;
    cfg.role       = UMESH_ROLE_COORDINATOR;
    cfg.security   = UMESH_SEC_NONE; /* simplified for loopback test */
    cfg.channel    = 6;
    cfg.tx_power   = 60;

    r = umesh_init(&cfg);
    TEST_ASSERT(r == UMESH_OK, "e2e: umesh_init OK");

    r = umesh_start();
    TEST_ASSERT(r == UMESH_OK, "e2e: umesh_start OK");

    {
        umesh_info_t info = umesh_get_info();
        TEST_ASSERT(info.node_id == UMESH_ADDR_COORDINATOR, "e2e: node_id == 0x01");
        TEST_ASSERT(info.net_id  == 0x01, "e2e: net_id == 0x01");
        TEST_ASSERT(info.state   == UMESH_STATE_CONNECTED, "e2e: state == CONNECTED");
    }
}

static void test_e2e_broadcast(void)
{
    umesh_cfg_t cfg;
    umesh_result_t r;

    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id     = 0x01;
    cfg.node_id    = UMESH_ADDR_COORDINATOR;
    cfg.master_key = TEST_KEY;
    cfg.role       = UMESH_ROLE_COORDINATOR;
    cfg.security   = UMESH_SEC_NONE;
    cfg.channel    = 6;
    cfg.tx_power   = 60;

    umesh_init(&cfg);
    umesh_start();

    s_rx_count  = 0;
    s_cmd_count = 0;
    umesh_on_receive(on_rx);
    umesh_on_cmd(UMESH_CMD_PING, on_ping);

    r = umesh_broadcast(UMESH_CMD_PING, NULL, 0);
    TEST_ASSERT(r == UMESH_OK, "e2e: broadcast PING OK");

    /* Via loopback, we receive our own broadcast */
    TEST_ASSERT(s_rx_count >= 1, "e2e: received via loopback");
    TEST_ASSERT(s_cmd_count >= 1, "e2e: CMD_PING handler called");
}

static void test_e2e_send_payload(void)
{
    umesh_cfg_t cfg;
    umesh_result_t r;
    float temp = 23.5f;

    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id     = 0x01;
    cfg.node_id    = UMESH_ADDR_COORDINATOR;
    cfg.master_key = TEST_KEY;
    cfg.role       = UMESH_ROLE_COORDINATOR;
    cfg.security   = UMESH_SEC_NONE;
    cfg.channel    = 6;
    cfg.tx_power   = 60;

    umesh_init(&cfg);
    umesh_start();

    r = umesh_broadcast(UMESH_CMD_SENSOR_TEMP, &temp, sizeof(temp));
    TEST_ASSERT(r == UMESH_OK, "e2e: broadcast sensor data OK");
}

static void test_e2e_err_str(void)
{
    TEST_ASSERT(strcmp(umesh_err_str(UMESH_OK),           "OK")           == 0,
                "err_str: OK");
    TEST_ASSERT(strcmp(umesh_err_str(UMESH_ERR_NOT_INIT), "NOT_INIT")     == 0,
                "err_str: NOT_INIT");
    TEST_ASSERT(strcmp(umesh_err_str(UMESH_ERR_MIC_FAIL), "MIC_FAIL")     == 0,
                "err_str: MIC_FAIL");
    TEST_ASSERT(strcmp(umesh_err_str(UMESH_ERR_REPLAY),   "REPLAY")       == 0,
                "err_str: REPLAY");
}

static void test_e2e_not_init(void)
{
    /* Reset and try to send without init */
    umesh_reset();
    umesh_result_t r = umesh_send(0x02, UMESH_CMD_PING, NULL, 0, 0);
    TEST_ASSERT(r == UMESH_ERR_NOT_INIT, "e2e: send before init -> NOT_INIT");
}

static void test_e2e_get_stats(void)
{
    umesh_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id     = 0x01;
    cfg.node_id    = UMESH_ADDR_COORDINATOR;
    cfg.master_key = TEST_KEY;
    cfg.role       = UMESH_ROLE_COORDINATOR;
    cfg.security   = UMESH_SEC_NONE;
    cfg.channel    = 6;
    cfg.tx_power   = 60;

    umesh_init(&cfg);
    umesh_start();
    umesh_broadcast(UMESH_CMD_PING, NULL, 0);
    umesh_broadcast(UMESH_CMD_PING, NULL, 0);

    {
        umesh_stats_t stats = umesh_get_stats();
        TEST_ASSERT(stats.tx_count >= 2, "e2e: stats tx_count >= 2");
    }
}

int main(void)
{
    printf("=== test_e2e ===\n");
    test_e2e_init_start();
    test_e2e_broadcast();
    test_e2e_send_payload();
    test_e2e_err_str();
    test_e2e_not_init();
    test_e2e_get_stats();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
