#include "cca.h"

/* Stub — will be implemented in Step 9 */
static int8_t  s_last_rssi        = -100;
static bool    s_rx_in_progress   = false;

void cca_init(void)
{
    s_last_rssi      = -100;
    s_rx_in_progress = false;
}

bool cca_channel_free(void)
{
    return !s_rx_in_progress &&
           (s_last_rssi < UMESH_CCA_THRESHOLD);
}

void cca_set_rssi(int8_t rssi)
{
    s_last_rssi = rssi;
}

void cca_set_rx_in_progress(bool in_progress)
{
    s_rx_in_progress = in_progress;
}
