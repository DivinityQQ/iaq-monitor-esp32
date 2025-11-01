/* components/app_config/include/iaq_config.h */
#ifndef IAQ_CONFIG_H
#define IAQ_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
/* Deliberately minimal: do not pull in IDF headers here. */

/**
 * Version information
 * These may be provided via compiler definitions from CMake (PROJECT_VERSION_*).
 * Fallbacks below are only used if not supplied by the build system.
 */
#ifndef IAQ_VERSION_MAJOR
#define IAQ_VERSION_MAJOR  0
#endif
#ifndef IAQ_VERSION_MINOR
#define IAQ_VERSION_MINOR  7
#endif
#ifndef IAQ_VERSION_PATCH
#define IAQ_VERSION_PATCH  6
#endif

/**
 * Task priorities (higher number = higher priority)
 * Based on project summary architecture
 */
#define TASK_PRIORITY_SENSOR_COORDINATOR    5
#define TASK_PRIORITY_MQTT_MANAGER          3
#define TASK_PRIORITY_DISPLAY               2
#define TASK_PRIORITY_STATUS_LED            1

/**
 * Task stack sizes (bytes)
 */
#define TASK_STACK_SENSOR_COORDINATOR   4096
#define TASK_STACK_MQTT_MANAGER         4096  /* Increased from 3072 due to cJSON stack usage */
#define TASK_STACK_DISPLAY              3072
#define TASK_STACK_STATUS_LED           2048
#define TASK_STACK_WEB_SERVER           6144

/**
 * Task core affinity (ESP32-S3 is dual-core)
 * Core 0 (PRO_CPU): Sensors, UI, LED
 * Core 1 (APP_CPU): Network/MQTT
 */
#define TASK_CORE_SENSOR_COORDINATOR    0
#define TASK_CORE_MQTT_MANAGER          1
#define TASK_CORE_DISPLAY               0
#define TASK_CORE_STATUS_LED            0
#define TASK_CORE_WEB_SERVER            1

/**
 * Event bits for inter-task synchronization
 * Used with FreeRTOS EventGroup
 */
#define WIFI_CONNECTED_BIT                  BIT0
#define MQTT_CONNECTED_BIT                  BIT1
#define SENSOR_COORDINATOR_STARTED_BIT      BIT2  /* Set when coordinator task starts (not when all sensors ready) */
#define DISPLAY_UPDATE_BIT                  BIT4
/* Set when system time is synchronized (SNTP) */
#define TIME_SYNCED_BIT                     BIT3

/**
 * Status publishing interval (milliseconds)
 */
#define STATUS_PUBLISH_INTERVAL_MS  30000  /* 30 seconds */

#endif /* IAQ_CONFIG_H */
