#include "umesh.h"
#include <stdio.h>
#include <string.h>

static const uint8_t KEY[16] = {
    0x2B,0x7E,0x15,0x16,0x28,0xAE,0xD2,0xA6,
    0xAB,0xF7,0x15,0x88,0x09,0xCF,0x4F,0x3C
};

static void on_receive(umesh_pkt_t *pkt)
{
    if (pkt->cmd == UMESH_CMD_SENSOR_TEMP && pkt->payload_len == sizeof(float)) {
        float t;
        memcpy(&t, pkt->payload, sizeof(t));
        printf("Temp from 0x%02X: %.1f C (RSSI %d)\n",
               pkt->src, (double)t, pkt->rssi);
    }
}

void app_main(void)
{
    umesh_cfg_t cfg = {
        .net_id = 0x01,
        .node_id = UMESH_ADDR_COORDINATOR,
        .master_key = KEY,
        .role = UMESH_ROLE_COORDINATOR,
        .security = UMESH_SEC_FULL,
        .channel = 6,
        .routing = UMESH_ROUTING_GRADIENT,
    };

    if (umesh_init(&cfg) != UMESH_OK) return;
    umesh_on_receive(on_receive);
    if (umesh_start() != UMESH_OK) return;

    while (1) {
        umesh_gradient_refresh();
        /* vTaskDelay(pdMS_TO_TICKS(30000)); */
    }
}
