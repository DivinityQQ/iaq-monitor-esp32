/* main/iaq_config.h */
#ifndef IAQ_CONFIG_H
#define IAQ_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Version information */
#define IAQ_VERSION_MAJOR  0
#define IAQ_VERSION_MINOR  1
#define IAQ_VERSION_PATCH  0

/* Task priorities (higher number = higher priority) */
#define TASK_PRIORITY_SENSORS         3
#define TASK_PRIORITY_UI              2
#define TASK_PRIORITY_WEB_SERVER      1

/* Task stack sizes */
#define TASK_STACK_SENSORS           4096
#define TASK_STACK_UI                3072
#define TASK_STACK_WEB_SERVER        4096

/* Task core affinity */
#define TASK_CORE_SENSORS            0  /* PRO_CPU for sensor tasks */
#define TASK_CORE_UI                 0
#define TASK_CORE_WEB_SERVER         1

/* Event bits for synchronization */
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define MQTT_CONNECTED_BIT    BIT2
#define SENSORS_READY_BIT     BIT3

/* System state */
typedef enum {
    SYSTEM_STATE_INIT,
    SYSTEM_STATE_CONNECTING,
    SYSTEM_STATE_RUNNING,
    SYSTEM_STATE_ERROR,
    SYSTEM_STATE_MAINTENANCE
} system_state_t;

/* Sensor data structure (placeholder for future) */
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float co2;
    float pm1_0;
    float pm2_5;
    float pm10;
    float voc_index;
    float nox_index;
    uint32_t timestamp;
} sensor_data_t;

/* Global system info */
typedef struct {
    system_state_t state;
    bool wifi_connected;
    bool mqtt_connected;
    int32_t wifi_rssi;
    uint32_t uptime_seconds;
    uint32_t free_heap;
    uint32_t min_free_heap;
    sensor_data_t sensors;
} system_info_t;

/* Global system info instance */
extern system_info_t g_system_info;

/* Helper macros for logging */
#define IAQ_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define IAQ_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define IAQ_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#define IAQ_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)

/* Utility macros */
#define IAQ_CHECK(x, msg, ret) do {                     \
        esp_err_t __err = (x);                          \
        if (__err != ESP_OK) {                          \
            ESP_LOGE(TAG, "%s: %s", msg,               \
                    esp_err_to_name(__err));           \
            return (ret);                               \
        }                                               \
    } while(0)

#endif /* IAQ_CONFIG_H */