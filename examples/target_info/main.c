#include <stdio.h>
#include <string.h>

#include "../../include/umesh.h"

static const uint8_t KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

void app_main(void)
{
    umesh_cfg_t cfg;
    umesh_info_t info;

    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id = 0x01;
    cfg.node_id = UMESH_ADDR_COORDINATOR;
    cfg.master_key = KEY;
    cfg.role = UMESH_ROLE_COORDINATOR;
    cfg.security = UMESH_SEC_NONE;
    cfg.channel = 6;

    if (umesh_init(&cfg) != UMESH_OK) {
        return;
    }

    info = umesh_get_info();
    printf("Target:   %s\n", umesh_get_target());
    printf("WiFi gen: %u\n", (unsigned)umesh_get_wifi_gen());
    printf("BT:       %s\n", umesh_target_supports(UMESH_CAP_BT) ? "yes" : "no");
    printf("TWT:      %s\n", umesh_target_supports(UMESH_CAP_TWT) ? "yes" : "no");
    printf("Max TX:   %.1f dBm\n", info.tx_power_max / 4.0f);
}
