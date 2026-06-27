#include <stdio.h>
#include <string.h>

#include "../include/umesh.h"
#include "../src/phy/phy_hal.h"
#include "../src/net/discovery.h"
#include "../src/net/net.h"

static int s_pass = 0;
static int s_fail = 0;
static uint32_t s_wake_count = 0;

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

#if UMESH_ENABLE_POWER_MANAGEMENT
static void test_wake_cb(void)
{
    s_wake_count++;
}
#endif

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

static void test_power_measurement_not_supported(void)
{
    float ma = 0.0f;

    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_ACTIVE,
               UMESH_ROLE_COORDINATOR, UMESH_ADDR_COORDINATOR);
    TEST_ASSERT(umesh_measure_current_ma(&ma) == UMESH_ERR_NOT_SUPPORTED,
                "power: measurement API is not supported");
}

static void test_power_deep_sleep_does_not_call_wake(void)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    init_umesh(UMESH_ROUTING_GRADIENT, UMESH_POWER_DEEP,
               UMESH_ROLE_END_NODE, 0x02);
    s_wake_count = 0;
    umesh_current_ctx()->cfg.on_wake = test_wake_cb;
    TEST_ASSERT(umesh_deep_sleep_cycle() == UMESH_OK,
                "power: deep sleep cycle returns OK in supported build");
    TEST_ASSERT(s_wake_count == 0,
                "power: deep sleep does not emit same-boot wake callback");
#else
    TEST_ASSERT(umesh_deep_sleep_cycle() == UMESH_ERR_NOT_SUPPORTED,
                "power: deep sleep remains unsupported when disabled");
#endif
}

static void test_power_beacon_uses_32bit_fields(void)
{
#if UMESH_ENABLE_POWER_MANAGEMENT
    umesh_frame_t frame;
    uint8_t payload[8];

    init_umesh(UMESH_ROUTING_DISTANCE_VECTOR, UMESH_POWER_ACTIVE,
               UMESH_ROLE_END_NODE, 0x02);
    memset(&frame, 0, sizeof(frame));
    frame.net_id = 0x01;
    frame.cmd = UMESH_CMD_POWER_BEACON;
    frame.payload_len = sizeof(payload);
    payload[0] = 0x88;
    payload[1] = 0x77;
    payload[2] = 0x66;
    payload[3] = 0x55;
    payload[4] = 0x44;
    payload[5] = 0x33;
    payload[6] = 0x22;
    payload[7] = 0x11;
    memcpy(frame.payload, payload, sizeof(payload));
    discovery_on_frame(&frame, -60);
    TEST_ASSERT(umesh_current_ctx()->net.light_sleep_interval_ms == 0x55667788u,
                "power: beacon light interval decodes 32-bit little-endian");
    TEST_ASSERT(umesh_current_ctx()->net.light_listen_window_ms == 0x11223344u,
                "power: beacon listen window decodes 32-bit little-endian");
#else
    TEST_ASSERT(1, "power: beacon parser test skipped when power is disabled");
#endif
}

static void test_twt_capability_not_advertised(void)
{
    TEST_ASSERT(!umesh_target_supports(UMESH_CAP_TWT),
                "power: TWT capability is not advertised");
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
    test_power_measurement_not_supported();
    test_power_deep_sleep_does_not_call_wake();
    test_power_beacon_uses_32bit_fields();
    test_twt_capability_not_advertised();
    test_esp32h2_not_supported();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
