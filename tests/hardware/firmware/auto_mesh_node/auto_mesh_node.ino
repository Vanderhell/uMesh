/*
 * uMesh Auto Mesh Node (ESP32-S3 / ESP32-C3)
 *
 * Flash this same firmware to all nodes.
 * Role is elected at runtime via UMESH_ROLE_AUTO.
 */

#include <Arduino.h>
#include <string.h>
#include "../../../../include/umesh.h"

#define NET_ID     0x01
#define CHANNEL    6
#define TX_POWER   52

static const uint8_t MASTER_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

static volatile bool s_send_pong = false;
static volatile uint8_t s_pong_dst = 0;
static char s_cmd_buf[16];
static uint8_t s_cmd_len = 0;

static void json_ready(void) {
    Serial.printf("{\"event\":\"ready\","
                  "\"data\":{\"mode\":\"auto\",\"state\":\"connected\","
                  "\"node_id\":%u,\"channel\":%u,\"net_id\":%u}}\n",
                  umesh_get_info().node_id,
                  umesh_get_info().channel,
                  umesh_get_info().net_id);
}

static void json_status(void) {
    umesh_info_t info = umesh_get_info();
    Serial.printf("{\"event\":\"status\","
                  "\"data\":{\"mode\":\"auto\",\"state\":\"connected\","
                  "\"node_id\":%u,\"channel\":%u,\"net_id\":%u}}\n",
                  info.node_id, info.channel, info.net_id);
}

static void json_elected(umesh_role_t role) {
    const char *name = "router";
    if (role == UMESH_ROLE_COORDINATOR) name = "coordinator";
    Serial.printf("{\"event\":\"elected\","
                  "\"data\":{\"role\":\"%s\"}}\n", name);
}

static void json_error(umesh_result_t code) {
    Serial.printf("{\"event\":\"error\","
                  "\"data\":{\"code\":%d,\"msg\":\"%s\"}}\n",
                  code, umesh_err_str(code));
}

static void on_role_elected(umesh_role_t role) {
    json_elected(role);
}

static void on_receive(umesh_pkt_t *pkt) {
    if (!pkt) return;
    if (pkt->cmd == UMESH_CMD_PING) {
        s_pong_dst = pkt->src;
        s_send_pong = true;
    }
}

void setup(void) {
    Serial.begin(115200);
    delay(200);

    umesh_cfg_t cfg = {
        .net_id = NET_ID,
        .node_id = UMESH_ADDR_UNASSIGNED,
        .master_key = MASTER_KEY,
        .role = UMESH_ROLE_AUTO,
        .security = UMESH_SEC_FULL,
        .channel = CHANNEL,
        .tx_power = TX_POWER,
        .scan_ms = 2000,
        .election_ms = 1000,
        .on_role_elected = on_role_elected,
    };

    umesh_result_t r = umesh_init(&cfg);
    if (r != UMESH_OK) { json_error(r); return; }

    umesh_on_receive(on_receive);

    r = umesh_start();
    if (r != UMESH_OK) { json_error(r); return; }

    json_ready();
}

void loop(void) {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_cmd_len == 0) continue;
            s_cmd_buf[s_cmd_len] = '\0';
            if (strcmp(s_cmd_buf, "STATUS") == 0) {
                json_status();
            } else if (strcmp(s_cmd_buf, "READY") == 0) {
                json_ready();
            }
            s_cmd_len = 0;
            continue;
        }

        if (s_cmd_len < (sizeof(s_cmd_buf) - 1)) {
            s_cmd_buf[s_cmd_len++] = c;
        }
    }

    umesh_tick(millis());

    if (s_send_pong) {
        s_send_pong = false;
        umesh_result_t r = umesh_send_cmd(s_pong_dst, UMESH_CMD_PONG, 0);
        if (r != UMESH_OK) json_error(r);
    }

    delay(5);
}
