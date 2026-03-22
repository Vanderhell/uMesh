/*
 * uMesh — Coordinator Example
 *
 * Coordinator node: assigns NODE_IDs, maintains routing table,
 * broadcasts ROUTE_UPDATE every 30s, handles sensor data.
 *
 * Platform: ESP32 (ESP-IDF)
 */

#include "umesh.h"
#include <stdio.h>

/* Pre-shared master key — identical on all nodes in the network */
static const uint8_t MASTER_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

/* Called when any packet is received */
static void on_receive(umesh_pkt_t *pkt)
{
    printf("[COORD] RX from 0x%02X cmd=0x%02X len=%u rssi=%d dBm\n",
           pkt->src, pkt->cmd, pkt->payload_len, pkt->rssi);
}

/* Called when a temperature sensor reading arrives */
static void on_temperature(umesh_pkt_t *pkt)
{
    float temp;
    if (pkt->payload_len < sizeof(float)) return;
    /* Note: direct cast assumes same endianness */
    __builtin_memcpy(&temp, pkt->payload, sizeof(float));
    printf("[COORD] Temperature from 0x%02X: %.1f C\n", pkt->src, (double)temp);
}

void app_main(void)
{
    umesh_result_t r;

    umesh_cfg_t cfg = {
        .net_id     = 0x01,
        .node_id    = UMESH_ADDR_COORDINATOR,  /* 0x01 */
        .master_key = MASTER_KEY,
        .role       = UMESH_ROLE_COORDINATOR,
        .security   = UMESH_SEC_FULL,
        .channel    = 6,
        .tx_power   = 60,                      /* 15 dBm */
    };

    r = umesh_init(&cfg);
    if (r != UMESH_OK) {
        printf("[COORD] init failed: %s\n", umesh_err_str(r));
        return;
    }

    umesh_on_receive(on_receive);
    umesh_on_cmd(UMESH_CMD_SENSOR_TEMP, on_temperature);

    r = umesh_start();
    if (r != UMESH_OK) {
        printf("[COORD] start failed: %s\n", umesh_err_str(r));
        return;
    }

    printf("[COORD] Running as coordinator (NET_ID=0x01, NODE_ID=0x01)\n");

    /* Application loop */
    while (1) {
        umesh_info_t info = umesh_get_info();
        printf("[COORD] Nodes in network: %u\n", info.node_count);

        /* ESP-IDF: replace with vTaskDelay or appropriate sleep */
        /* vTaskDelay(pdMS_TO_TICKS(10000)); */
    }
}
