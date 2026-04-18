idf_component_register(
    SRCS "iaq_json.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__cjson iaq_data sensor_coordinator time_sync app_config power_board
    PRIV_REQUIRES freertos esp_timer
)
