/* components/sensor_coordinator/include/sensor_coordinator.h */
#ifndef SENSOR_COORDINATOR_H
#define SENSOR_COORDINATOR_H

#include "esp_err.h"
#include "system_context.h"

typedef enum {
    SENSOR_ID_MCU = 0,
    SENSOR_ID_SHT45,
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
 * @param ctx System context containing event group for synchronization
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sensor_coordinator_init(iaq_system_context_t *ctx);

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

/* Disable a sensor (stops reading, calls driver disable, transitions to DISABLED state). */
esp_err_t sensor_coordinator_disable(sensor_id_t id);

/* Enable a sensor (calls driver enable, transitions back to appropriate state). */
esp_err_t sensor_coordinator_enable(sensor_id_t id);

/* Synchronous force read with completion feedback. Waits up to timeout_ms. */
esp_err_t sensor_coordinator_force_read_sync(sensor_id_t id, uint32_t timeout_ms);

/* Get current cadences (ms) and whether each was loaded from NVS at startup.
 * Pass NULL for any array you don't need. Arrays must have SENSOR_ID_MAX length. */
esp_err_t sensor_coordinator_get_cadences(uint32_t out_ms[SENSOR_ID_MAX], bool out_from_nvs[SENSOR_ID_MAX]);

/** State machine types (for console/observability) **/

typedef enum {
    SENSOR_STATE_UNINIT = 0,
    SENSOR_STATE_INIT,
    SENSOR_STATE_WARMING,
    SENSOR_STATE_READY,
    SENSOR_STATE_ERROR,
    SENSOR_STATE_DISABLED
} sensor_state_t;

typedef struct {
    sensor_state_t state;
    int64_t warmup_deadline_us;
    int64_t last_read_us;
    uint32_t error_count;
} sensor_runtime_info_t;

/**
 * Get runtime state of a sensor (for console observability).
 *
 * @param id Sensor ID
 * @param out_info Pointer to store runtime info
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if id invalid
 */
esp_err_t sensor_coordinator_get_runtime_info(sensor_id_t id, sensor_runtime_info_t *out_info);

/**
 * Convert sensor_state_t to string.
 */
const char* sensor_coordinator_state_to_string(sensor_state_t state);

/**
 * Returns true if any sensor has reached READY state.
 */
bool sensor_coordinator_any_ready(void);

/**
 * Convert sensor ID to name string.
 *
 * @param id Sensor ID
 * @return Sensor name string (e.g., "mcu", "s8", "sht45")
 */
const char* sensor_coordinator_id_to_name(sensor_id_t id);

/**
 * Get warm-up duration for a sensor (from Kconfig).
 *
 * @param id Sensor ID
 * @return Warm-up duration in milliseconds, or 0 if invalid ID
 */
uint32_t sensor_coordinator_get_warmup_ms(sensor_id_t id);

#endif /* SENSOR_COORDINATOR_H */
