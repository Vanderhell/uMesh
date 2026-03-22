/*
 * uMesh — End Node Example
 *
 * Sends temperature sensor readings to the coordinator every 5 seconds.
 * Automatically joins the network on startup.
 *
 * Platform: ESP32 (ESP-IDF)
 */

#include "umesh.h"
#include <stdio.h>

/* Pre-shared master key — must match coordinator and all other nodes */
static const uint8_t MASTER_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

/* Called when the coordinator sends a command to this node */
static void on_receive(umesh_pkt_t *pkt)
{
    printf("[NODE] RX from 0x%02X cmd=0x%02X\n", pkt->src, pkt->cmd);
}

/* Called when a SET_INTERVAL command is received */
static void on_set_interval(umesh_pkt_t *pkt)
{
    uint32_t interval_ms;
    if (pkt->payload_len < sizeof(uint32_t)) return;
    __builtin_memcpy(&interval_ms, pkt->payload, sizeof(uint32_t));
    printf("[NODE] New send interval: %u ms\n", (unsigned int)interval_ms);
}

/*
 * Simulate reading a temperature sensor.
 * Replace with actual sensor driver (e.g. DS18B20, SHT31).
 */
static float read_temperature(void)
{
    return 23.5f; /* placeholder */
}

void app_main(void)
{
    umesh_result_t r;
    umesh_info_t   info;

    umesh_cfg_t cfg = {
        .net_id     = 0x01,
        .node_id    = UMESH_ADDR_UNASSIGNED, /* auto-assigned by coordinator */
        .master_key = MASTER_KEY,
        .role       = UMESH_ROLE_END_NODE,
        .security   = UMESH_SEC_FULL,
        .channel    = 6,
        .tx_power   = 60,
    };

    r = umesh_init(&cfg);
    if (r != UMESH_OK) {
        printf("[NODE] init failed: %s\n", umesh_err_str(r));
        return;
    }

    umesh_on_receive(on_receive);
    umesh_on_cmd(UMESH_CMD_SET_INTERVAL, on_set_interval);

    /* Join the mesh network — sends CMD_JOIN broadcast */
    r = umesh_start();
    if (r != UMESH_OK) {
        printf("[NODE] join failed: %s\n", umesh_err_str(r));
        return;
    }

    /* Wait until joined (coordinator assigns NODE_ID) */
    /* ESP-IDF: use vTaskDelay + polling loop */
    info = umesh_get_info();
    printf("[NODE] Joined with NODE_ID=0x%02X (NET_ID=0x%02X)\n",
           info.node_id, info.net_id);

    /* Send sensor data periodically */
    while (1) {
        float temp = read_temperature();

        r = umesh_send(UMESH_ADDR_COORDINATOR,
                       UMESH_CMD_SENSOR_TEMP,
                       &temp, sizeof(temp),
                       UMESH_FLAG_ACK_REQ);

        if (r == UMESH_OK) {
            printf("[NODE] Sent temperature: %.1f C\n", (double)temp);
        } else {
            printf("[NODE] Send failed: %s\n", umesh_err_str(r));
        }

        /* ESP-IDF: vTaskDelay(pdMS_TO_TICKS(5000)); */
    }
}
