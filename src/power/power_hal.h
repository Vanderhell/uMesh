#ifndef UMESH_POWER_HAL_H
#define UMESH_POWER_HAL_H

#include "../common/defs.h"

void  power_hal_light_sleep(uint32_t duration_ms);
void  power_hal_deep_sleep(uint32_t duration_ms);
float power_hal_get_vcc(void);

#endif /* UMESH_POWER_HAL_H */
