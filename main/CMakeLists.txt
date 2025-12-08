# main/CMakeLists.txt

idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        display_oled
        driver
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        iaq_data
        system_context
        connectivity
        sensor_coordinator
        console_commands
        power_board
        app_config
        iaq_profiler
        web_portal
        ota_manager
)

# Create LittleFS image for the web portal if source dir exists
set(WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/../www")
if (EXISTS ${WEB_DIR})
    message(STATUS "Including LittleFS web content from: ${WEB_DIR}")
    littlefs_create_partition_image(www ${WEB_DIR} FLASH_IN_PROJECT)
else()
    message(STATUS "No web dir '${WEB_DIR}' found; LittleFS image will be empty or preflashed")
endif()
