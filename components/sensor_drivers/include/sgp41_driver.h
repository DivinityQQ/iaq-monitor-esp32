/* components/sensor_drivers/include/sgp41_driver.h */
#ifndef SGP41_DRIVER_H
#define SGP41_DRIVER_H

#include "esp_err.h"
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
 * @param temp_c Temperature for compensation (from SHT41)
 * @param humidity_rh Humidity for compensation (from SHT41)
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

#ifdef __cplusplus
}
#endif

#endif /* SGP41_DRIVER_H */
