set(SRCS
    "i2c_bus.c"
    "uart_bus.c"
    "mcu_temp_driver.c"
    "sht45_driver.c"
    "bmp280_driver.c"
    "sgp41_driver.c"
    "pms5003_driver.c"
    "s8_driver.c"
)

# Conditionally add simulation backend
if(CONFIG_IAQ_SIMULATION)
    list(APPEND SRCS "sensor_sim.c")
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer sensirion
)
