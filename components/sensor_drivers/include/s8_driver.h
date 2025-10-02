/* components/sensor_drivers/include/s8_driver.h */
#ifndef S8_DRIVER_H
#define S8_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Senseair S8 CO2 sensor driver.
 * Configures UART port using Kconfig settings.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t s8_driver_init(void);

/**
 * Read CO2 concentration from S8 sensor.
 *
 * @param out_co2_ppm Pointer to store CO2 value in ppm
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t s8_driver_read_co2(float *out_co2_ppm);

/**
 * Calibrate CO2 sensor to a known reference value.
 *
 * @param target_ppm Target CO2 concentration in ppm (typically 400 for fresh air)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t s8_driver_calibrate_co2(int target_ppm);

/**
 * Reset the S8 sensor (flush UART buffers).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t s8_driver_reset(void);

/**
 * Deinitialize the S8 driver and release UART resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t s8_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* S8_DRIVER_H */
