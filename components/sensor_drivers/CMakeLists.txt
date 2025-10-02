idf_component_register(
    SRCS
        "i2c_bus.c"
        "uart_bus.c"
        "mcu_temp_driver.c"
        "s8_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)
