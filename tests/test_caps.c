#include <stdio.h>
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
            printf("  FAIL: %s (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

static const uint8_t TEST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static void init_umesh(void)
{
    umesh_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id = 0x01;
    cfg.node_id = UMESH_ADDR_COORDINATOR;
    cfg.master_key = TEST_KEY;
    cfg.role = UMESH_ROLE_COORDINATOR;
    cfg.security = UMESH_SEC_NONE;
    cfg.channel = 6;
    cfg.routing = UMESH_ROUTING_DISTANCE_VECTOR;
    cfg.power_mode = UMESH_POWER_ACTIVE;
    (void)umesh_init(&cfg);
    (void)umesh_start();
}

static void test_caps_target_defined(void)
{
    TEST_ASSERT(UMESH_TARGET[0] != '\0', "caps: target string defined");
}

static void test_caps_tx_power_range(void)
{
    TEST_ASSERT(UMESH_TX_POWER_MAX >= 8 && UMESH_TX_POWER_MAX <= 80,
                "caps: tx power max range");
}

static void test_caps_supports_wifi(void)
{
#if UMESH_HAS_WIFI
    TEST_ASSERT(umesh_target_supports(UMESH_CAP_WIFI),
                "caps: target supports wifi");
#else
    TEST_ASSERT(!umesh_target_supports(UMESH_CAP_WIFI),
                "caps: no-wifi target handled gracefully");
#endif
}

static void test_caps_info_target_field(void)
{
    umesh_info_t info = umesh_get_info();
    TEST_ASSERT(info.target != NULL, "caps: info target non-null");
    TEST_ASSERT(strcmp(info.target, UMESH_TARGET) == 0,
                "caps: info target matches macro");
}

static void test_caps_wifi_gen_valid(void)
{
    uint8_t gen = umesh_get_wifi_gen();
    TEST_ASSERT(gen == 4 || gen == 6, "caps: wifi gen valid");
}

static void test_caps_low_memory_tables(void)
{
#if UMESH_RAM_KB < 400
    TEST_ASSERT(UMESH_MAX_NODES <= 8, "caps: low-memory nodes reduced");
#else
    TEST_ASSERT(UMESH_MAX_NODES >= 16, "caps: normal-memory nodes default");
#endif
}

int main(void)
{
    printf("=== test_caps ===\n");
    init_umesh();
    test_caps_target_defined();
    test_caps_tx_power_range();
    test_caps_supports_wifi();
    test_caps_info_target_field();
    test_caps_wifi_gen_valid();
    test_caps_low_memory_tables();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
