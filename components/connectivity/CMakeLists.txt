# components/connectivity/CMakeLists.txt
idf_component_register(
    SRCS
        "wifi_manager.c"
        "mqtt_manager.c"
       
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "../../main"
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        mqtt
        json
        iaq_data
        system_context
        sensor_coordinator
        time_sync
    PRIV_REQUIRES
        esp_timer
        driver
        esp_event
)

# Optional TLS assets (embedded PEM files). Guard with existence checks and
# define compile-time flags to let C code know what is available.

if(CONFIG_IAQ_MQTT_TLS_TRUST_CA_PEM)
    set(_CA_PEM "${CMAKE_CURRENT_LIST_DIR}/certs/ca.pem")
    if(EXISTS "${_CA_PEM}")
        message(STATUS "Embedding MQTT Root CA: ${_CA_PEM}")
        target_add_binary_data(${COMPONENT_TARGET} "${_CA_PEM}" TEXT)
        target_compile_definitions(${COMPONENT_TARGET} PRIVATE IAQ_HAS_CA_PEM=1)
    else()
        message(WARNING "IAQ_MQTT_TLS_TRUST_CA_PEM is enabled but ca.pem not found at: ${_CA_PEM}")
    endif()
endif()

if(CONFIG_IAQ_MQTT_MTLS_ENABLE)
    set(_CLI_CERT "${CMAKE_CURRENT_LIST_DIR}/certs/client.crt.pem")
    set(_CLI_KEY  "${CMAKE_CURRENT_LIST_DIR}/certs/client.key.pem")
    if(EXISTS "${_CLI_CERT}")
        message(STATUS "Embedding MQTT client certificate: ${_CLI_CERT}")
        target_add_binary_data(${COMPONENT_TARGET} "${_CLI_CERT}" TEXT)
        target_compile_definitions(${COMPONENT_TARGET} PRIVATE IAQ_HAS_CLIENT_CERT=1)
    else()
        message(WARNING "IAQ_MQTT_MTLS_ENABLE is enabled but client.crt.pem not found at: ${_CLI_CERT}")
    endif()
    if(EXISTS "${_CLI_KEY}")
        message(STATUS "Embedding MQTT client key: ${_CLI_KEY}")
        target_add_binary_data(${COMPONENT_TARGET} "${_CLI_KEY}" TEXT)
        target_compile_definitions(${COMPONENT_TARGET} PRIVATE IAQ_HAS_CLIENT_KEY=1)
    else()
        message(WARNING "IAQ_MQTT_MTLS_ENABLE is enabled but client.key.pem not found at: ${_CLI_KEY}")
    endif()
endif()
