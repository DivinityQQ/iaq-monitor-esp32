idf_component_register(
    SRCS "console_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES console iaq_data sensor_coordinator display_oled app_config
    PRIV_REQUIRES connectivity esp_timer esp_partition spi_flash
)
