#include "../../src/power/power_hal.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(uint32_t ms) { Sleep(ms); }
#else
#include <unistd.h>
static void sleep_ms(uint32_t ms) { usleep(ms * 1000u); }
#endif

void power_hal_light_sleep(uint32_t duration_ms)
{
    sleep_ms(duration_ms);
}

void power_hal_deep_sleep(uint32_t duration_ms)
{
    sleep_ms(duration_ms);
}

float power_hal_get_vcc(void)
{
    return 3.30f;
}
