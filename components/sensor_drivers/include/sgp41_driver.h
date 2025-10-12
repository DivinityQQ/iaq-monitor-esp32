/* components/sensor_drivers/include/sgp41_driver.h */
#ifndef SGP41_DRIVER_H
#define SGP41_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SGP41 VOC/NOx sensor driver.
 * Uses I2C bus (must be initialized before calling this).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sgp41_driver_init(void);

/**
 * Read VOC and NOx indices from SGP41 sensor.
 * Note: SGP41 requires ~10 seconds of continuous operation for conditioning.
 * Indices use Sensirion's algorithm and range from 0-500.
 *
 * @param out_voc_index Pointer to store VOC index (can be NULL)
 * @param out_nox_index Pointer to store NOx index (can be NULL)
 * @param temp_c Temperature for compensation (from SHT4x)
 * @param humidity_rh Humidity for compensation (from SHT4x)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t sgp41_driver_read(uint16_t *out_voc_index, uint16_t *out_nox_index,
                             float temp_c, float humidity_rh);

/**
 * Reset the SGP41 sensor (soft reset via I2C command).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sgp41_driver_reset(void);

/**
 * Deinitialize the SGP41 driver.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sgp41_driver_deinit(void);

/**
 * Perform a conditioning tick during warm-up (max 10s), using compensation.
 * Safe to call at ~1 Hz while sensor is in WARMING state.
 * After 10s from driver init/reset this becomes a no-op.
 */
esp_err_t sgp41_driver_conditioning_tick(float temp_c, float humidity_rh);

/**
 * Returns true once SGP41 is ready to report non-zero Gas Index values.
 * This accounts for both the 10s conditioning and the algorithm's initial
 * blackout (about 45s). Before that, the driver will return ESP_ERR_NOT_SUPPORTED
 * on reads and the coordinator should keep the sensor in WARMING.
 */
bool sgp41_driver_is_reporting_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* SGP41_DRIVER_H */
