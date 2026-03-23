/*
 * µMesh Hardware Integration Test — Coordinator (ESP32-S3)
 *
 * Role: Runs 7 automated tests after router and end_node join.
 * Outputs JSON events on Serial (115200 baud) for the Python runner.
 *
 * Expected topology:
 *   coordinator (0x01) <--> router (0x02) <--> end_node (0x03)
 *
 * JSON events emitted:
 *   {"event":"ready",       "data":{...}}
 *   {"event":"joined",      "data":{...}}
 *   {"event":"tx",          "data":{...}}
 *   {"event":"rx",          "data":{...}}
 *   {"event":"test_result", "data":{...}}
 *   {"event":"stats",       "data":{...}}
 *   {"event":"error",       "data":{...}}
 */

#include <Arduino.h>
#include "../../../../include/umesh.h"

/* ── Network configuration ─────────────────────────────────────────────── */
#define NET_ID        0x01
#define CHANNEL       6
#define TX_POWER      52        /* ~13 dBm */
#define NODE_ROUTER   0x02
#define NODE_ENDNODE  0x03
/* Nodes use hardcoded IDs (0x02=router, 0x03=end_node) — no JOIN wait */

static const uint8_t MASTER_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

/* ── Test parameters ───────────────────────────────────────────────────── */
#define SINGLE_HOP_COUNT   50
#define MULTI_HOP_COUNT    50
#define BROADCAST_COUNT    10
#define STRESS_COUNT       200
#define PONG_TIMEOUT_MS    500
#define ACK_TIMEOUT_MS     200

/* ── Shared state (accessed from both WiFi task and main task) ─────────── */
static volatile bool     s_pong_received    = false;
static volatile int8_t   s_last_rssi        = 0;
static volatile uint8_t  s_pong_src         = 0;

/* ── Internal counters ─────────────────────────────────────────────────── */
static uint32_t s_test_rx_count  = 0;
static uint32_t s_test_tx_count  = 0;

/* ── JSON helpers ──────────────────────────────────────────────────────── */

static void json_ready(void) {
    Serial.printf("{\"event\":\"ready\","
                  "\"data\":{\"role\":\"coordinator\","
                  "\"node_id\":1,\"channel\":%d}}\n", CHANNEL);
}

static void json_tx(uint8_t dst, uint8_t cmd, uint8_t size) {
    Serial.printf("{\"event\":\"tx\","
                  "\"data\":{\"dst\":%u,\"cmd\":\"0x%02X\",\"size\":%u}}\n",
                  dst, cmd, size);
}

static void json_rx(uint8_t src, uint8_t cmd, int8_t rssi) {
    Serial.printf("{\"event\":\"rx\","
                  "\"data\":{\"src\":%u,\"cmd\":\"0x%02X\","
                  "\"rssi\":%d}}\n", src, cmd, rssi);
}

static void json_test_result(const char *name, bool pass,
                              int latency_ms, uint32_t delivered,
                              uint32_t total) {
    Serial.printf("{\"event\":\"test_result\","
                  "\"data\":{\"test\":\"%s\",\"pass\":%s,"
                  "\"latency_ms\":%d,"
                  "\"delivered\":%lu,\"total\":%lu}}\n",
                  name, pass ? "true" : "false",
                  latency_ms, delivered, total);
}

static void json_stats(void) {
    umesh_stats_t s = umesh_get_stats();
    Serial.printf("{\"event\":\"stats\","
                  "\"data\":{\"tx\":%lu,\"rx\":%lu,"
                  "\"ack\":%lu,\"retry\":%lu,\"drop\":%lu}}\n",
                  s.tx_count, s.rx_count,
                  s.ack_count, s.retry_count, s.drop_count);
}

static void json_error(umesh_result_t code) {
    Serial.printf("{\"event\":\"error\","
                  "\"data\":{\"code\":%d,\"msg\":\"%s\"}}\n",
                  code, umesh_err_str(code));
}

/* ── RX callback (runs in WiFi task — no umesh_send() here!) ──────────── */

static void on_receive(umesh_pkt_t *pkt) {
    json_rx(pkt->src, pkt->cmd, pkt->rssi);

    if (pkt->cmd == UMESH_CMD_PONG) {
        s_pong_src      = pkt->src;
        s_last_rssi     = pkt->rssi;
        s_pong_received = true;   /* processed in main task */
    }

    s_test_rx_count++;
}

/* ── Wait helpers ──────────────────────────────────────────────────────── */

/* Wait for a PONG from any node; returns RTT in ms or -1 on timeout */
static int wait_pong(uint32_t timeout_ms) {
    s_pong_received = false;
    uint32_t t0 = millis();
    while (!s_pong_received) {
        if ((millis() - t0) > timeout_ms) return -1;
        delay(1);
    }
    return (int)(millis() - t0);
}

/* ── Test implementations ──────────────────────────────────────────────── */

/* Test 1 — connectivity: PING each node, wait for PONG */
static void test_connectivity(void) {
    uint8_t nodes[] = {NODE_ROUTER, NODE_ENDNODE};
    int total_latency = 0;
    uint32_t delivered = 0;

    for (int i = 0; i < 2; i++) {
        json_tx(nodes[i], UMESH_CMD_PING, 0);
        umesh_result_t r = umesh_send_cmd(nodes[i], UMESH_CMD_PING, 0);
        s_test_tx_count++;
        if (r != UMESH_OK) { json_error(r); continue; }

        int rtt = wait_pong(PONG_TIMEOUT_MS);
        if (rtt >= 0) { delivered++; total_latency += rtt; }
    }

    int avg_lat = (delivered > 0) ? (total_latency / (int)delivered) : -1;
    json_test_result("connectivity", delivered == 2, avg_lat, delivered, 2);
}

/* Test 2 — single_hop: 50 SENSOR_TEMP sends to router with ACK */
static void test_single_hop(void) {
    uint32_t delivered = 0;
    uint8_t payload[2] = {0x00, 0x64};  /* fake 10.0 °C */

    for (int i = 0; i < SINGLE_HOP_COUNT; i++) {
        json_tx(NODE_ROUTER, UMESH_CMD_SENSOR_TEMP, sizeof(payload));
        umesh_result_t r = umesh_send(NODE_ROUTER, UMESH_CMD_SENSOR_TEMP,
                                      payload, sizeof(payload),
                                      UMESH_FLAG_ACK_REQ);
        s_test_tx_count++;
        if (r == UMESH_OK) delivered++;
        else                json_error(r);
        delay(20);
    }

    bool pass = (delivered * 100 / SINGLE_HOP_COUNT) >= 85;
    json_test_result("single_hop", pass, -1, delivered, SINGLE_HOP_COUNT);
}

/* Test 3 — multi_hop: 50 SENSOR_TEMP sends to end_node via router */
static void test_multi_hop(void) {
    uint32_t delivered = 0;
    uint8_t payload[2] = {0x00, 0xC8};  /* fake 20.0 °C */

    for (int i = 0; i < MULTI_HOP_COUNT; i++) {
        json_tx(NODE_ENDNODE, UMESH_CMD_SENSOR_TEMP, sizeof(payload));
        umesh_result_t r = umesh_send(NODE_ENDNODE, UMESH_CMD_SENSOR_TEMP,
                                      payload, sizeof(payload),
                                      UMESH_FLAG_ACK_REQ);
        s_test_tx_count++;
        if (r == UMESH_OK) delivered++;
        else                json_error(r);
        delay(30);
    }

    bool pass = (delivered * 100 / MULTI_HOP_COUNT) >= 75;
    json_test_result("multi_hop", pass, -1, delivered, MULTI_HOP_COUNT);
}

/* Test 4 — broadcast: 10 broadcast PINGs */
static void test_broadcast(void) {
    uint32_t delivered = 0;

    for (int i = 0; i < BROADCAST_COUNT; i++) {
        json_tx(UMESH_ADDR_BROADCAST, UMESH_CMD_PING, 0);
        umesh_result_t r = umesh_broadcast(UMESH_CMD_PING, NULL, 0);
        s_test_tx_count++;
        if (r == UMESH_OK) delivered++;
        else                json_error(r);
        delay(50);
    }

    bool pass = delivered >= (BROADCAST_COUNT - 1);
    json_test_result("broadcast", pass, -1, delivered, BROADCAST_COUNT);
}

/* Test 5 — security: encrypted send with ACK_REQ */
static void test_security(void) {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};

    json_tx(NODE_ROUTER, UMESH_CMD_SENSOR_RAW, sizeof(payload));
    umesh_result_t r = umesh_send(NODE_ROUTER, UMESH_CMD_SENSOR_RAW,
                                  payload, sizeof(payload),
                                  UMESH_FLAG_ACK_REQ | UMESH_FLAG_ENCRYPTED);
    s_test_tx_count++;
    bool pass = (r == UMESH_OK);
    if (!pass) json_error(r);
    json_test_result("security", pass, -1, pass ? 1 : 0, 1);
}

/* Test 6 — stress: 200 rapid sends to router without ACK */
static void test_stress(void) {
    uint32_t delivered = 0;
    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};

    for (int i = 0; i < STRESS_COUNT; i++) {
        umesh_result_t r = umesh_send(NODE_ROUTER, UMESH_CMD_SENSOR_RAW,
                                      payload, sizeof(payload), 0);
        s_test_tx_count++;
        if (r == UMESH_OK) delivered++;
        delay(5);
    }

    bool pass = (delivered * 100 / STRESS_COUNT) >= 80;
    json_test_result("stress", pass, -1, delivered, STRESS_COUNT);
}

/* Test 7 — rssi: PING each node, report RSSI from PONG, always passes */
static void test_rssi(void) {
    uint8_t nodes[] = {NODE_ROUTER, NODE_ENDNODE};
    uint32_t delivered = 0;
    int total_rssi = 0;

    for (int i = 0; i < 2; i++) {
        json_tx(nodes[i], UMESH_CMD_PING, 0);
        umesh_result_t r = umesh_send_cmd(nodes[i], UMESH_CMD_PING, 0);
        s_test_tx_count++;
        if (r != UMESH_OK) { json_error(r); continue; }

        int rtt = wait_pong(PONG_TIMEOUT_MS);
        if (rtt >= 0) {
            delivered++;
            total_rssi += s_last_rssi;
            /* Report per-node RSSI as an rx event — already logged in cb */
        }
        delay(50);
    }

    /* RSSI test always passes (informational) */
    json_test_result("rssi", true, -1, delivered, 2);
}

/* ── Arduino entry points ──────────────────────────────────────────────── */

void setup(void) {
    Serial.begin(115200);
    delay(200);

    umesh_cfg_t cfg = {
        .net_id     = NET_ID,
        .node_id    = UMESH_ADDR_COORDINATOR,
        .master_key = MASTER_KEY,
        .role       = UMESH_ROLE_COORDINATOR,
        .security   = UMESH_SEC_FULL,
        .channel    = CHANNEL,
        .tx_power   = TX_POWER,
    };

    umesh_result_t r = umesh_init(&cfg);
    if (r != UMESH_OK) { json_error(r); return; }

    umesh_on_receive(on_receive);

    r = umesh_start();
    if (r != UMESH_OK) { json_error(r); return; }

    json_ready();

    /* Nodes have hardcoded IDs — wait for them to announce via ROUTE_UPDATE */
    delay(3000);

    /* Run all 7 tests sequentially */
    test_connectivity();
    test_single_hop();
    test_multi_hop();
    test_broadcast();
    test_security();
    test_stress();
    test_rssi();

    /* Emit final stats — Python runner uses this as completion signal */
    json_stats();
}

void loop(void) {
    umesh_tick(millis());
    delay(100);
}
