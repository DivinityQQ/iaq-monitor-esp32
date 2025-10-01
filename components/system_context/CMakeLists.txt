# components/system_context/CMakeLists.txt
idf_component_register(
    SRCS
        "system_context.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_event
)
