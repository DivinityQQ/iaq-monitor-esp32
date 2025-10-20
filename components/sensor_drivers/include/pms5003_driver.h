/* components/sensor_drivers/include/pms5003_driver.h */
#ifndef PMS5003_DRIVER_H
#define PMS5003_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the PMS5003 particulate matter sensor driver.
 * Configures UART port using Kconfig settings.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pms5003_driver_init(void);

/**
 * Read particulate matter concentrations from PMS5003 sensor.
 *
 * @param out_pm1_0 Pointer to store PM1.0 in µg/m³ (can be NULL)
 * @param out_pm2_5 Pointer to store PM2.5 in µg/m³ (can be NULL)
 * @param out_pm10 Pointer to store PM10 in µg/m³ (can be NULL)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if stub, error code otherwise
 */
esp_err_t pms5003_driver_read(float *out_pm1_0, float *out_pm2_5, float *out_pm10);

/**
 * Reset the PMS5003 sensor (flush UART buffers, send wake command).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pms5003_driver_reset(void);

/**
 * Deinitialize the PMS5003 driver and release UART resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pms5003_driver_deinit(void);

/**
 * Disable the PMS5003 sensor (put to sleep via SET pin if configured).
 * If SET pin is not configured, warns but still returns success.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pms5003_driver_disable(void);

/**
 * Enable the PMS5003 sensor (wake from sleep via SET pin if configured).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pms5003_driver_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* PMS5003_DRIVER_H */
