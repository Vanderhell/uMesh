#include "../../src/power/power_hal.h"
#if UMESH_ENABLE_POWER_MANAGEMENT

#if defined(CONFIG_IDF_TARGET_ESP32)
#define UMESH_HAS_LIGHT_SLEEP 1
#define UMESH_HAS_DEEP_SLEEP  1
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define UMESH_HAS_LIGHT_SLEEP 1
#define UMESH_HAS_DEEP_SLEEP  1
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define UMESH_HAS_LIGHT_SLEEP 1
#define UMESH_HAS_DEEP_SLEEP  1
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define UMESH_HAS_LIGHT_SLEEP 1
#define UMESH_HAS_DEEP_SLEEP  1
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define UMESH_HAS_LIGHT_SLEEP 1
#define UMESH_HAS_DEEP_SLEEP  1
#define UMESH_HAS_TARGET_WAKE_TIME 1
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
#define UMESH_HAS_WIFI 0
#error "ESP32-H2 does not support WiFi. uMesh requires WiFi."
#else
#define UMESH_HAS_LIGHT_SLEEP 0
#define UMESH_HAS_DEEP_SLEEP  0
#endif

#if UMESH_HAS_LIGHT_SLEEP || UMESH_HAS_DEEP_SLEEP
#include "esp_sleep.h"
#include "esp_wifi.h"
#endif

void power_hal_light_sleep(uint32_t duration_ms)
{
#if UMESH_HAS_LIGHT_SLEEP
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_sleep_enable_timer_wakeup((uint64_t)duration_ms * 1000ULL);
    esp_light_sleep_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
#else
    UMESH_UNUSED(duration_ms);
#endif
}

void power_hal_deep_sleep(uint32_t duration_ms)
{
#if UMESH_HAS_DEEP_SLEEP
    esp_sleep_enable_timer_wakeup((uint64_t)duration_ms * 1000ULL);
    esp_deep_sleep_start();
#else
    UMESH_UNUSED(duration_ms);
#endif
}

float power_hal_get_vcc(void)
{
    return 3.30f;
}
#else
void power_hal_light_sleep(uint32_t duration_ms)
{
    UMESH_UNUSED(duration_ms);
}

void power_hal_deep_sleep(uint32_t duration_ms)
{
    UMESH_UNUSED(duration_ms);
}

float power_hal_get_vcc(void)
{
    return 0.0f;
}
#endif
