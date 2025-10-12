idf_component_register(SRCS "sensor_coordinator.c"
                             "sensor_fusion.c"
                             "metrics_calc.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../main"
                       REQUIRES freertos iaq_data esp_timer sensor_drivers nvs_flash system_context time_sync)
