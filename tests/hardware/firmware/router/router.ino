/*
 * µMesh Hardware Integration Test — Router (ESP32-S3)
 *
 * Role: Joins the network, forwards packets, responds to PING with PONG.
 * Outputs JSON events on Serial (115200 baud) for the Python runner.
 *
 * Expected topology:
 *   coordinator (0x01) <--> router (0x02) <--> end_node (0x03)
 */

#include <Arduino.h>
#include <string.h>
#include "../../../../include/umesh.h"

/* ── Network configuration ─────────────────────────────────────────────── */
#define NET_ID     0x01
#define CHANNEL    6
#define TX_POWER   52        /* ~13 dBm */

static const uint8_t MASTER_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

/* ── Pending responses (set in RX callback, sent in loop) ─────────────── */
static volatile bool    s_send_pong      = false;
static volatile uint8_t s_pong_dst       = 0;
static char             s_cmd_buf[24];
static uint8_t          s_cmd_len = 0;

/* ── JSON helpers ──────────────────────────────────────────────────────── */

static void json_ready(uint8_t node_id) {
    Serial.printf("{\"event\":\"ready\","
                  "\"data\":{\"role\":\"router\","
                  "\"node_id\":%u,\"channel\":%d}}\n",
                  node_id, CHANNEL);
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

static void json_error(umesh_result_t code) {
    Serial.printf("{\"event\":\"error\","
                  "\"data\":{\"code\":%d,\"msg\":\"%s\"}}\n",
                  code, umesh_err_str(code));
}

static void handle_serial_command(const char *cmd) {
    if (strcmp(cmd, "READY") == 0) {
        json_ready(umesh_get_info().node_id);
    }
}

static void poll_serial_commands(void) {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (s_cmd_len == 0) continue;
            s_cmd_buf[s_cmd_len] = '\0';
            handle_serial_command(s_cmd_buf);
            s_cmd_len = 0;
            continue;
        }

        if (s_cmd_len < (sizeof(s_cmd_buf) - 1)) {
            s_cmd_buf[s_cmd_len++] = c;
        }
    }
}

/* ── RX callback (WiFi task — no umesh_send() here!) ──────────────────── */

static void on_receive(umesh_pkt_t *pkt) {
    json_rx(pkt->src, pkt->cmd, pkt->rssi);

    if (pkt->cmd == UMESH_CMD_PING) {
        s_pong_dst  = pkt->src;
        s_send_pong = true;
    }
}

/* ── Arduino entry points ──────────────────────────────────────────────── */

void setup(void) {
    Serial.begin(115200);
    delay(200);

    umesh_cfg_t cfg = {
        .net_id     = NET_ID,
        .node_id    = 0x02,
        .master_key = MASTER_KEY,
        .role       = UMESH_ROLE_ROUTER,
        .security   = UMESH_SEC_FULL,
        .channel    = CHANNEL,
        .tx_power   = TX_POWER,
    };

    umesh_result_t r = umesh_init(&cfg);
    if (r != UMESH_OK) { json_error(r); return; }

    umesh_on_receive(on_receive);

    r = umesh_start();
    if (r != UMESH_OK) { json_error(r); return; }

    /* pre-assigned node_id — connects immediately */
    uint32_t t0 = millis();
    while (umesh_get_info().state != UMESH_STATE_CONNECTED) {
        if ((millis() - t0) > 15000) {
            Serial.println("{\"event\":\"error\","
                           "\"data\":{\"code\":-1,"
                           "\"msg\":\"join timeout\"}}");
            return;
        }
        delay(100);
    }

    json_ready(umesh_get_info().node_id);
}

void loop(void) {
    poll_serial_commands();
    umesh_tick(millis());

    /* Process pending PONG response */
    if (s_send_pong) {
        s_send_pong = false;
        uint8_t dst = s_pong_dst;
        json_tx(dst, UMESH_CMD_PONG, 0);
        umesh_result_t r = umesh_send_cmd(dst, UMESH_CMD_PONG, 0);
        if (r != UMESH_OK) json_error(r);
    }

    delay(5);
}
