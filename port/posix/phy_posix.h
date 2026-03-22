#ifndef UMESH_PHY_POSIX_H
#define UMESH_PHY_POSIX_H

#include <stdbool.h>

/* Test helpers — POSIX port only */
void phy_posix_set_loopback(bool enabled);
void phy_posix_flush(void);

#endif /* UMESH_PHY_POSIX_H */
