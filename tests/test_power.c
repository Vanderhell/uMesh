#include <stdio.h>
#include <string.h>

#include "../include/umesh.h"
#include "../src/phy/phy_hal.h"

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

static void init_umesh(umesh_routing_mode_t routing, umesh_power_mode_t power,
                       umesh_role_t role, uint8_t node_id)
{
    umesh_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.net_id = 0x01;
    cfg.node_id = node_id;
    cfg.master_key = TEST_KEY;
    cfg.role = role;
    cfg.security = UMESH_SEC_NONE;
    cfg.channel = 6;
    cfg.routing = routing;
    cfg.power_mode = power;
    cfg.light_sleep_interval_ms = 1000;
    cfg.light_listen_window_ms = 100;
    cfg.deep_sleep_tx_interval_ms = 30000;
    umesh_init(&cfg);
    umesh_start();
}

static void test_power_mode_default(void)
{
    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_ACTIVE,
               UMESH_ROLE_COORDINATOR, UMESH_ADDR_COORDINATOR);
    TEST_ASSERT(umesh_get_power_mode() == UMESH_POWER_ACTIVE,
                "power: default active mode");
}

static void test_power_estimate_active(void)
{
    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_ACTIVE,
               UMESH_ROLE_COORDINATOR, UMESH_ADDR_COORDINATOR);
#if UMESH_ENABLE_POWER_MANAGEMENT
    TEST_ASSERT(umesh_estimate_current_ma() > 50.0f,
                "power: active estimate > 50mA");
#else
    TEST_ASSERT(umesh_estimate_current_ma() < 0.0f,
                "power: disabled estimate is sentinel");
#endif
}

static void test_power_estimate_light(void)
{
    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_LIGHT,
               UMESH_ROLE_END_NODE, 0x02);
    {
        float ma = umesh_estimate_current_ma();
#if UMESH_ENABLE_POWER_MANAGEMENT
        TEST_ASSERT(ma > 7.0f && ma < 9.0f,
                    "power: light estimate ~8mA");
#else
        TEST_ASSERT(ma < 0.0f, "power: light estimate disabled");
#endif
    }
}

static void test_power_estimate_deep(void)
{
    init_umesh(UMESH_ROUTING_GRADIENT, UMESH_POWER_DEEP,
               UMESH_ROLE_END_NODE, 0x02);
#if UMESH_ENABLE_POWER_MANAGEMENT
    TEST_ASSERT(umesh_estimate_current_ma() < 5.0f,
                "power: deep estimate < 5mA");
#else
    TEST_ASSERT(umesh_estimate_current_ma() < 0.0f,
                "power: deep estimate disabled");
#endif
}

static void test_power_stats_tracking(void)
{
    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_LIGHT,
               UMESH_ROLE_END_NODE, 0x02);
    umesh_tick(0);
    umesh_tick(500);
    umesh_tick(1500);
    umesh_tick(2600);
    {
        umesh_power_stats_t st = umesh_get_power_stats();
#if UMESH_ENABLE_POWER_MANAGEMENT
        TEST_ASSERT(st.sleep_count > 0, "power: sleep_count increments");
        TEST_ASSERT(st.total_sleep_ms > 0, "power: total_sleep_ms increments");
        TEST_ASSERT(st.duty_cycle_pct > 0.0f && st.duty_cycle_pct < 100.0f,
                    "power: duty cycle tracked");
#else
        TEST_ASSERT(st.sleep_count == 0, "power: disabled sleep_count zero");
        TEST_ASSERT(st.total_sleep_ms == 0, "power: disabled total_sleep_ms zero");
        TEST_ASSERT(st.total_active_ms == 0, "power: disabled total_active_ms zero");
#endif
    }
}

static void test_power_deep_sleep_requires_gradient(void)
{
    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_DEEP,
               UMESH_ROLE_END_NODE, 0x02);
#if UMESH_ENABLE_POWER_MANAGEMENT
    TEST_ASSERT(umesh_deep_sleep_cycle() == UMESH_ERR_NOT_ROUTABLE,
                "power: deep sleep requires gradient routing");
#else
    TEST_ASSERT(umesh_deep_sleep_cycle() == UMESH_ERR_NOT_SUPPORTED,
                "power: deep sleep not supported when disabled");
#endif
}

static void test_esp32h2_not_supported(void)
{
#if defined(CONFIG_IDF_TARGET_ESP32H2)
#error "ESP32-H2 does not support WiFi. uMesh requires WiFi."
#endif
    TEST_ASSERT(1, "power: ESP32-H2 guard documented");
}

int main(void)
{
    printf("=== test_power ===\n");
    test_power_mode_default();
    test_power_estimate_active();
    test_power_estimate_light();
    test_power_estimate_deep();
    test_power_stats_tracking();
    test_power_deep_sleep_requires_gradient();
    test_esp32h2_not_supported();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
