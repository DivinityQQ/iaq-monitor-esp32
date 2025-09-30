idf_component_register(
    SRCS "console_commands.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../main"
    REQUIRES console iaq_data sensor_coordinator
    PRIV_REQUIRES connectivity esp_timer esp_partition spi_flash
)
