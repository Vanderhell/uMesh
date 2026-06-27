idf_component_register(
    SRCS
        "phy_esp32.c"
        "power_esp32.c"
        "twt_esp32c6.c"
    INCLUDE_DIRS
        "../../include"
        "../../src"
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        freertos
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE UMESH_PORT_ESP32=1)
