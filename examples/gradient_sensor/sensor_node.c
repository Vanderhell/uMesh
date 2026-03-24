#include "umesh.h"
#include <stdio.h>

static const uint8_t KEY[16] = {
    0x2B,0x7E,0x15,0x16,0x28,0xAE,0xD2,0xA6,
    0xAB,0xF7,0x15,0x88,0x09,0xCF,0x4F,0x3C
};

static void on_ready(uint8_t distance)
{
    printf("Gradient ready: %u hops to coordinator\n", distance);
}

static float read_sensor(void)
{
    return 23.5f;
}

void app_main(void)
{
    umesh_cfg_t cfg = {
        .net_id = 0x01,
        .node_id = UMESH_ADDR_UNASSIGNED,
        .master_key = KEY,
        .role = UMESH_ROLE_AUTO,
        .security = UMESH_SEC_FULL,
        .channel = 6,
        .routing = UMESH_ROUTING_GRADIENT,
        .on_gradient_ready = on_ready,
    };

    if (umesh_init(&cfg) != UMESH_OK) return;
    if (umesh_start() != UMESH_OK) return;

    while (1) {
        float t = read_sensor();
        umesh_send(UMESH_ADDR_COORDINATOR, UMESH_CMD_SENSOR_TEMP,
                   &t, sizeof(t), UMESH_FLAG_ACK_REQ);
        /* vTaskDelay(pdMS_TO_TICKS(5000)); */
    }
}
