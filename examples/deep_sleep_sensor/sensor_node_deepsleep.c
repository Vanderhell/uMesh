#include "umesh.h"

static const uint8_t KEY[16] = {
    0x2B,0x7E,0x15,0x16,0x28,0xAE,0xD2,0xA6,
    0xAB,0xF7,0x15,0x88,0x09,0xCF,0x4F,0x3C
};

static float read_temperature(void)
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
        .routing = UMESH_ROUTING_GRADIENT,
        .security = UMESH_SEC_FULL,
        .channel = 6,
        .power_mode = UMESH_POWER_DEEP,
        .deep_sleep_tx_interval_ms = 60000,
    };

    if (umesh_init(&cfg) != UMESH_OK) return;
    if (umesh_start() != UMESH_OK) return;

    {
        float temp = read_temperature();
        umesh_send(UMESH_ADDR_COORDINATOR,
                   UMESH_CMD_SENSOR_TEMP,
                   &temp, sizeof(temp),
                   UMESH_FLAG_ACK_REQ);
    }

    umesh_deep_sleep_cycle();
}
