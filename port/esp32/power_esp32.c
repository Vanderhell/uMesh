#include "../../src/power/power_hal.h"
#if UMESH_ENABLE_POWER_MANAGEMENT

#if !UMESH_HAS_WIFI
#error "uMesh ESP32 port requires WiFi-capable target."
#endif

#include "esp_sleep.h"
#include "esp_wifi.h"
#ifdef UMESH_HAS_TWT
#include "twt_esp32c6.h"
#endif

void power_hal_light_sleep(uint32_t duration_ms)
{
#ifdef UMESH_HAS_TWT
    if (twt_esp32c6_schedule_ms(duration_ms) == UMESH_OK) {
        return;
    }
#endif
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_sleep_enable_timer_wakeup((uint64_t)duration_ms * 1000ULL);
    esp_light_sleep_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void power_hal_deep_sleep(uint32_t duration_ms)
{
    esp_sleep_enable_timer_wakeup((uint64_t)duration_ms * 1000ULL);
    esp_deep_sleep_start();
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
