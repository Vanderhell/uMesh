#ifndef UMESH_PHY_POSIX_H
#define UMESH_PHY_POSIX_H

#include <stdbool.h>
#include <stdint.h>

/* Test helpers — POSIX port only */
void phy_posix_set_loopback(bool enabled);
void phy_posix_flush(void);
void phy_posix_set_delay_hook(void (*hook)(uint32_t duration_ms));

#endif /* UMESH_PHY_POSIX_H */
