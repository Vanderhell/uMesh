#ifndef UMESH_CCA_H
#define UMESH_CCA_H

#include "../common/defs.h"
#include <stdbool.h>

void cca_init(void);
bool cca_channel_free(void);
void cca_set_rssi(int8_t rssi);
void cca_set_rx_in_progress(bool in_progress);

#endif /* UMESH_CCA_H */
