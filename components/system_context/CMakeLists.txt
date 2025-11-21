# components/system_context/CMakeLists.txt
idf_component_register(
    SRCS
        "system_context.c"
        "pm_guard.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_event
        esp_pm
)
