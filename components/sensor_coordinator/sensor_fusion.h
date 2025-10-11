/* components/sensor_coordinator/sensor_fusion.h */
#ifndef SENSOR_FUSION_H
#define SENSOR_FUSION_H

#include "esp_err.h"
#include "iaq_data.h"

/**
 * Initialize sensor fusion subsystem.
 * Loads calibration coefficients from NVS or uses Kconfig defaults.
 * Initializes ABC baseline tracking state.
 *
 * @return ESP_OK on success, ESP_FAIL on initialization error
 */
esp_err_t fusion_init(void);

/**
 * Apply cross-sensor compensations to raw sensor data.
 *
 * This function performs the following compensations in order:
 * 1. Temperature self-heating correction (SHT4x)
 * 2. PM humidity correction (PMS5003)
 * 3. CO2 pressure compensation (S8)
 * 4. CO2 ABC baseline correction (S8)
 *
 * Compensated values are written to data->fused.
 * Diagnostics are written to data->fusion_diag.
 *
 * All compensations are conditional on:
 * - Kconfig enable flags
 * - Valid input sensor readings
 * - Reasonable value ranges
 *
 * @param data Pointer to locked iaq_data structure (caller must hold mutex)
 */
void fusion_apply(iaq_data_t *data);

/**
 * Update CO2 ABC baseline tracking with new CO2 measurement.
 * Called internally by fusion_apply() when ABC is enabled.
 * Tracks nightly CO2 minima over 7 days to establish outdoor baseline.
 *
 * @param co2_ppm Current CO2 reading (after pressure compensation)
 * @param timestamp_us Current time in microseconds since boot
 */
void fusion_update_abc(float co2_ppm, int64_t timestamp_us);

/**
 * Reset CO2 ABC baseline tracking to defaults.
 * Use when sensor location changes or baseline becomes unreliable.
 *
 * @return ESP_OK on success
 */
esp_err_t fusion_reset_abc(void);

/**
 * Set PM RH correction coefficients (runtime calibration).
 * Coefficients are persisted to NVS.
 *
 * @param a Coefficient 'a' in formula: pm_dry = pm_raw / (1 + a*(RH/100)^b)
 * @param b Exponent 'b' in formula
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if values out of range
 */
esp_err_t fusion_set_pm_rh_coeffs(float a, float b);

/**
 * Set temperature self-heating offset (runtime calibration).
 * Offset is persisted to NVS.
 *
 * @param offset_c Temperature offset in degrees Celsius (positive if sensor reads high)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if offset unreasonable (|offset| > 10°C)
 */
esp_err_t fusion_set_temp_offset(float offset_c);

/**
 * Get current fusion status for diagnostics/console display.
 *
 * @param pm_rh_a Output: PM RH coefficient A
 * @param pm_rh_b Output: PM RH coefficient B
 * @param temp_offset Output: Temperature self-heating offset
 * @param abc_baseline Output: Current CO2 ABC baseline (ppm)
 * @param abc_confidence Output: ABC confidence (0-100%)
 */
void fusion_get_status(float *pm_rh_a, float *pm_rh_b, float *temp_offset,
                       uint16_t *abc_baseline, uint8_t *abc_confidence);

#endif /* SENSOR_FUSION_H */
