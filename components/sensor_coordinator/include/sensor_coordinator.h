/* components/sensor_coordinator/include/sensor_coordinator.h */
#ifndef SENSOR_COORDINATOR_H
#define SENSOR_COORDINATOR_H

#include "esp_err.h"

typedef enum {
    SENSOR_ID_MCU = 0,
    SENSOR_ID_SHT41,
    SENSOR_ID_BMP280,
    SENSOR_ID_SGP41,
    SENSOR_ID_PMS5003,
    SENSOR_ID_S8,
    SENSOR_ID_MAX
} sensor_id_t;

/**
 * Initialize the sensor coordinator.
 * Sets up sensor hardware interfaces (I2C, UART) and prepares for sensor init.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sensor_coordinator_init(void);

/**
 * Start the sensor coordinator task.
 * This task will:
 * - Initialize all sensors
 * - Read sensors periodically (every 30s)
 * - Apply compensation algorithms
 * - Update global iaq_data structure
 * - Signal DATA_READY event
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sensor_coordinator_start(void);

/**
 * Stop the sensor coordinator task.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sensor_coordinator_stop(void);

/** Coordinator control API (executed in coordinator task context) **/

/* Request immediate read of a sensor (updates iaq_data on success). */
esp_err_t sensor_coordinator_force_read(sensor_id_t id);

/* Request sensor reset (disable/enable or re-init), if supported. */
esp_err_t sensor_coordinator_reset(sensor_id_t id);

/* Request calibration operation (e.g., CO2 level) for a sensor, if supported. */
esp_err_t sensor_coordinator_calibrate(sensor_id_t id, int value);

/* Configure periodic cadence per sensor (ms). 0 disables periodic reads. */
esp_err_t sensor_coordinator_set_cadence(sensor_id_t id, uint32_t interval_ms);

/* Synchronous force read with completion feedback. Waits up to timeout_ms. */
esp_err_t sensor_coordinator_force_read_sync(sensor_id_t id, uint32_t timeout_ms);

/* Get current cadences (ms) and whether each was loaded from NVS at startup.
 * Pass NULL for any array you don't need. Arrays must have SENSOR_ID_MAX length. */
esp_err_t sensor_coordinator_get_cadences(uint32_t out_ms[SENSOR_ID_MAX], bool out_from_nvs[SENSOR_ID_MAX]);

#endif /* SENSOR_COORDINATOR_H */
