#include "cca.h"

/*
 * Carrier Sense (CCA) implementation.
 *
 * For the POSIX port: CCA always returns free (channel never busy)
 * unless s_rx_in_progress is set externally.
 *
 * For the ESP32 port: CSMA/CA is partially handled by WiFi hardware;
 * we add a software layer checking s_rx_in_progress and last RSSI.
 * (s_rx_in_progress is set by the promiscuous callback.)
 */

static int8_t s_last_rssi      = -100;
static bool   s_rx_in_progress = false;

void cca_init(void)
{
    s_last_rssi      = -100;
    s_rx_in_progress = false;
}

/*
 * Returns true if the channel appears free.
 * Channel is busy if:
 *   - Reception is currently in progress, OR
 *   - Last measured RSSI is above the CCA threshold
 *     (i.e. someone is transmitting)
 */
bool cca_channel_free(void)
{
    if (s_rx_in_progress) {
        return false;
    }
    /* RSSI below threshold (more negative) means channel is quiet */
    return s_last_rssi < UMESH_CCA_THRESHOLD;
}

void cca_set_rssi(int8_t rssi)
{
    s_last_rssi = rssi;
}

void cca_set_rx_in_progress(bool in_progress)
{
    s_rx_in_progress = in_progress;
}
