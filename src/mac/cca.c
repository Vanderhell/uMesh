#include "cca.h"
#include "../context.h"

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

void cca_init(void)
{
    umesh_ctx_t *ctx = umesh_current_ctx();
    ctx->mac.last_rssi = -100;
    ctx->mac.rx_in_progress = false;
}

/*
 * Returns true if the channel appears free.
 *
 * On ESP32, channel energy is not directly measurable outside of the
 * promiscuous callback.  Using s_last_rssi as a proxy is incorrect —
 * it reflects the RSSI of the *last received packet*, not the current
 * channel energy level, so it causes CCA to appear permanently busy
 * whenever a frame was recently received above the threshold.
 *
 * We therefore rely solely on s_rx_in_progress (set while on_phy_rx
 * is executing) to detect an active reception in progress.
 */
bool cca_channel_free(void)
{
    return !umesh_current_ctx()->mac.rx_in_progress;
}

void cca_set_rssi(int8_t rssi)
{
    umesh_current_ctx()->mac.last_rssi = rssi;
}

void cca_set_rx_in_progress(bool in_progress)
{
    umesh_current_ctx()->mac.rx_in_progress = in_progress;
}
