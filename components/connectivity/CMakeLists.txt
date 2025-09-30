# components/connectivity/CMakeLists.txt
idf_component_register(
    SRCS
        "wifi_manager.c"
        "mqtt_manager.c"
       
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "../../main"
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        mqtt
        json
        iaq_data
        system_context
    PRIV_REQUIRES
        esp_timer
        driver
        esp_event
)
