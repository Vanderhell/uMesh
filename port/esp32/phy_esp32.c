/*
 * ESP32 PHY port — raw 802.11
 * Implemented in Step 15.
 * This file will not compile without ESP-IDF.
 */
#ifdef UMESH_PORT_ESP32

#include "../../src/phy/phy_hal.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

static void (*s_rx_cb)(const uint8_t*, uint8_t, int8_t) = NULL;
static uint8_t s_net_id = 0;

/* 802.11 Data frame header template */
static const uint8_t S_80211_HDR[24] = {
    0x08, 0x00,                                  /* Frame Control: Data */
    0x00, 0x00,                                  /* Duration            */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,          /* Addr1: Broadcast    */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          /* Addr2: SRC          */
    0xAC, 0x00, 0x00, 0x00, 0x00, 0x00,          /* Addr3: BSSID/NET_ID */
    0x00, 0x00                                   /* Sequence control    */
};

static void IRAM_ATTR promisc_cb(void *buf,
                                  wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_DATA) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *frame = pkt->payload;

    /* Quick filter: uMesh prefix + NET_ID */
    if (frame[16] != 0xAC) return;
    if (frame[17] != 0x00) return;
    if (frame[18] != s_net_id) return;

    if (s_rx_cb) {
        s_rx_cb(&frame[24],
                (uint8_t)(pkt->rx_ctrl.sig_len - 24),
                pkt->rx_ctrl.rssi);
    }
}

umesh_result_t phy_hal_init(const umesh_phy_cfg_t *cfg)
{
    s_net_id = cfg->net_id;

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wcfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(cfg->channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_max_tx_power(cfg->tx_power);

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    esp_wifi_set_promiscuous(true);

    return UMESH_OK;
}

umesh_result_t phy_hal_send(const uint8_t *payload, uint8_t len)
{
    uint8_t frame[UMESH_MAX_FRAME_SIZE];

    memcpy(frame, S_80211_HDR, 24);
    esp_wifi_get_mac(WIFI_IF_STA, &frame[10]);
    frame[18] = s_net_id;
    memcpy(&frame[24], payload, len);

    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, frame,
                                       24 + len, true);
    return (err == ESP_OK) ? UMESH_OK : UMESH_ERR_HARDWARE;
}

void phy_hal_set_rx_cb(void (*cb)(const uint8_t *payload,
                                   uint8_t len,
                                   int8_t rssi))
{
    s_rx_cb = cb;
}

void phy_hal_deinit(void)
{
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    s_rx_cb = NULL;
}

#endif /* UMESH_PORT_ESP32 */
