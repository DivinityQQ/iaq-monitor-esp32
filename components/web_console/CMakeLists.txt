idf_component_register(
    SRCS
        "web_console.c"
        "ws_log.c"
        "ws_console.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        console
        freertos
    PRIV_REQUIRES
        esp_timer
        log
        iaq_profiler
        app_config
)
