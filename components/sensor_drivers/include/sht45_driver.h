/* components/sensor_drivers/include/sht45_driver.h */
#ifndef SHT45_DRIVER_H
#define SHT45_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SHT45 (SHT4x) temperature and humidity sensor driver.
 * Uses I2C bus (must be initialized before calling this).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht45_driver_init(void);

/**
 * Read temperature and humidity from SHT45 (SHT4x) sensor.
 *
 * @param out_temp_c Pointer to store temperature in Celsius (can be NULL)
 * @param out_humidity_rh Pointer to store humidity in %RH (can be NULL)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t sht45_driver_read(float *out_temp_c, float *out_humidity_rh);

/**
 * Reset the SHT45 (SHT4x) sensor (soft reset via I2C command).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht45_driver_reset(void);

/**
 * Deinitialize the SHT45 driver.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sht45_driver_deinit(void);

/**
 * Disable the SHT45 sensor (stub - no hardware sleep mode).
 *
 * @return ESP_OK
 */
esp_err_t sht45_driver_disable(void);

/**
 * Enable the SHT45 sensor (stub - no hardware sleep mode).
 *
 * @return ESP_OK
 */
esp_err_t sht45_driver_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* SHT45_DRIVER_H */
