# main/CMakeLists.txt

idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        display_oled
        driver
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        iaq_data
        system_context
        connectivity
        sensor_coordinator
        console_commands
        app_config
        iaq_profiler
        web_portal
)
