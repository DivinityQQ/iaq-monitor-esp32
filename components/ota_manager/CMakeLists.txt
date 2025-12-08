idf_component_register(
    SRCS "ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_partition esp_system esp_timer esp_app_format littlefs freertos
)
