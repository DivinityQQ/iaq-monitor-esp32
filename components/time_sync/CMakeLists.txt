idf_component_register(
    SRCS "time_sync.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../main"
    REQUIRES esp_netif esp_event system_context
)
