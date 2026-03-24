#ifndef UMESH_CAPS_H
#define UMESH_CAPS_H

/* Auto-detect from ESP-IDF target */
#if defined(CONFIG_IDF_TARGET_ESP32)
#define UMESH_TARGET        "ESP32"
#define UMESH_HAS_WIFI      1
#define UMESH_HAS_BT        1
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX  78
#endif
#ifndef UMESH_RAM_KB
#define UMESH_RAM_KB        520
#endif
#define UMESH_WIFI_GEN      4

#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define UMESH_TARGET        "ESP32-S2"
#define UMESH_HAS_WIFI      1
#define UMESH_HAS_BT        0
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX  78
#endif
#ifndef UMESH_RAM_KB
#define UMESH_RAM_KB        320
#endif
#define UMESH_WIFI_GEN      4

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define UMESH_TARGET        "ESP32-S3"
#define UMESH_HAS_WIFI      1
#define UMESH_HAS_BT        1
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX  78
#endif
#ifndef UMESH_RAM_KB
#define UMESH_RAM_KB        512
#endif
#define UMESH_WIFI_GEN      4

#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define UMESH_TARGET        "ESP32-C3"
#define UMESH_HAS_WIFI      1
#define UMESH_HAS_BT        1
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX  78
#endif
#ifndef UMESH_RAM_KB
#define UMESH_RAM_KB        400
#endif
#define UMESH_WIFI_GEN      4

#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define UMESH_TARGET        "ESP32-C6"
#define UMESH_HAS_WIFI      1
#define UMESH_HAS_BT        1
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX  80
#endif
#ifndef UMESH_RAM_KB
#define UMESH_RAM_KB        512
#endif
#define UMESH_WIFI_GEN      6
#define UMESH_HAS_TWT       1

#elif defined(CONFIG_IDF_TARGET_ESP32H2)
#error "ESP32-H2 does not support WiFi. uMesh requires WiFi."

#elif defined(CONFIG_IDF_TARGET_ESP32C2)
#error "ESP32-C2 has insufficient RAM for uMesh."

#else
#define UMESH_TARGET        "POSIX"
#define UMESH_HAS_WIFI      0
#define UMESH_HAS_BT        0
#ifndef UMESH_TX_POWER_MAX
#define UMESH_TX_POWER_MAX  78
#endif
#ifndef UMESH_RAM_KB
#define UMESH_RAM_KB        65535
#endif
#define UMESH_WIFI_GEN      4
#endif

#if defined(UMESH_PORT_ESP32) && defined(UMESH_HAS_WIFI) && (UMESH_HAS_WIFI == 0)
#error "This ESP32 target does not support WiFi."
#endif

#endif /* UMESH_CAPS_H */
