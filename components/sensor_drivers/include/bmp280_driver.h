/* components/sensor_drivers/include/bmp280_driver.h */
#ifndef BMP280_DRIVER_H
#define BMP280_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the BMP280 pressure and temperature sensor driver.
 * Uses I2C bus (must be initialized before calling this).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_driver_init(void);

/**
 * Read pressure and temperature from BMP280 sensor.
 *
 * @param out_pressure_hpa Pointer to store pressure in hPa (can be NULL)
 * @param out_temp_c Pointer to store temperature in Celsius (can be NULL)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t bmp280_driver_read(float *out_pressure_hpa, float *out_temp_c);

/**
 * Reset the BMP280 sensor (soft reset via I2C command).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_driver_reset(void);

/**
 * Deinitialize the BMP280 driver.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmp280_driver_deinit(void);

/**
 * Disable the BMP280 sensor (stub - no hardware sleep mode).
 *
 * @return ESP_OK
 */
esp_err_t bmp280_driver_disable(void);

/**
 * Enable the BMP280 sensor (stub - no hardware sleep mode).
 *
 * @return ESP_OK
 */
esp_err_t bmp280_driver_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_DRIVER_H */
