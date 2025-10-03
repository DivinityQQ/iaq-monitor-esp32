/* components/sensor_drivers/include/sht41_driver.h */
#ifndef SHT41_DRIVER_H
#define SHT41_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SHT41 temperature and humidity sensor driver.
 * Uses I2C bus (must be initialized before calling this).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht41_driver_init(void);

/**
 * Read temperature and humidity from SHT41 sensor.
 *
 * @param out_temp_c Pointer to store temperature in Celsius (can be NULL)
 * @param out_humidity_rh Pointer to store humidity in %RH (can be NULL)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t sht41_driver_read(float *out_temp_c, float *out_humidity_rh);

/**
 * Reset the SHT41 sensor (soft reset via I2C command).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht41_driver_reset(void);

/**
 * Deinitialize the SHT41 driver.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht41_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SHT41_DRIVER_H */
