/* main/iaq_config.h */
#ifndef IAQ_CONFIG_H
#define IAQ_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
/* Deliberately minimal: do not pull in IDF headers here. */

/**
 * Version information
 */
#define IAQ_VERSION_MAJOR  0
#define IAQ_VERSION_MINOR  3
#define IAQ_VERSION_PATCH  0

/**
 * Task priorities (higher number = higher priority)
 * Based on project summary architecture
 */
#define TASK_PRIORITY_SENSOR_COORDINATOR    5
#define TASK_PRIORITY_NETWORK_MANAGER       3
#define TASK_PRIORITY_DISPLAY               2
#define TASK_PRIORITY_STATUS_LED            1

/**
 * Task stack sizes (bytes)
 */
#define TASK_STACK_SENSOR_COORDINATOR   4096
#define TASK_STACK_NETWORK_MANAGER      3072
#define TASK_STACK_DISPLAY              3072
#define TASK_STACK_STATUS_LED           2048

/**
 * Task core affinity (ESP32-S3 is dual-core)
 * Core 0 (PRO_CPU): Sensors, UI, LED
 * Core 1 (APP_CPU): Network/MQTT
 */
#define TASK_CORE_SENSOR_COORDINATOR    0
#define TASK_CORE_NETWORK_MANAGER       1
#define TASK_CORE_DISPLAY               0
#define TASK_CORE_STATUS_LED            0

/**
 * Event bits for inter-task synchronization
 * Used with FreeRTOS EventGroup
 */
#define WIFI_CONNECTED_BIT          BIT0
#define MQTT_CONNECTED_BIT          BIT1
#define SENSORS_READY_BIT           BIT2
#define SENSORS_DATA_READY_BIT      BIT3
#define SENSORS_CALIBRATE_BIT       BIT4
#define DISPLAY_UPDATE_BIT          BIT5

/* Per-sensor update bits (set when a sensor completes a new reading) */
#define SENSOR_UPDATED_MCU_BIT       BIT6
#define SENSOR_UPDATED_SHT41_BIT     BIT7
#define SENSOR_UPDATED_BMP280_BIT    BIT8
#define SENSOR_UPDATED_SGP41_BIT     BIT9
#define SENSOR_UPDATED_PMS5003_BIT   BIT10
#define SENSOR_UPDATED_S8_BIT        BIT11

/**
 * Status publishing interval (milliseconds)
 */
#define STATUS_PUBLISH_INTERVAL_MS  30000  /* 30 seconds */

/**
 * Sensor state publish policy
 * - Coalesce multiple sensor updates within a small window
 * - Enforce a minimum interval between publishes
 */
#define SENSOR_PUBLISH_COALESCE_WINDOW_MS   300   /* wait up to 300ms to batch updates */
#define SENSOR_PUBLISH_MIN_INTERVAL_MS     1000   /* at most 1 publish per second */

#endif /* IAQ_CONFIG_H */
