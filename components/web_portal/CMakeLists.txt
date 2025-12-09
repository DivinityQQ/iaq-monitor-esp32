idf_component_register(
    SRCS "web_portal.c" "dns_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server app_update esp_partition littlefs json connectivity iaq_data iaq_json sensor_coordinator system_context app_config time_sync iaq_profiler power_board ota_manager web_console
    PRIV_REQUIRES freertos esp_timer
    EMBED_TXTFILES "certs/servercert.pem" "certs/prvtkey.pem"
)
