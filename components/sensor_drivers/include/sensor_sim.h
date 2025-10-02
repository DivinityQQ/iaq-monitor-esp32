/* components/sensor_drivers/include/sensor_sim.h */
#ifndef SENSOR_SIM_H
#define SENSOR_SIM_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_IAQ_SIMULATION

/**
 * Read simulated temperature.
 *
 * @param out_celsius Pointer to store temperature in Celsius
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_temperature(float *out_celsius);

/**
 * Read simulated MCU internal temperature.
 *
 * @param out_celsius Pointer to store temperature in Celsius
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_mcu_temperature(float *out_celsius);

/**
 * Read simulated humidity.
 *
 * @param out_rh Pointer to store humidity in %RH
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_humidity(float *out_rh);

/**
 * Read simulated pressure.
 *
 * @param out_hpa Pointer to store pressure in hPa
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_pressure(float *out_hpa);

/**
 * Read simulated CO2 concentration.
 *
 * @param out_ppm Pointer to store CO2 in ppm
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_co2(float *out_ppm);

/**
 * Read simulated VOC and NOx indices.
 *
 * @param out_voc Pointer to store VOC index (0-500)
 * @param out_nox Pointer to store NOx index (0-500)
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_voc_nox(uint16_t *out_voc, uint16_t *out_nox);

/**
 * Read simulated particulate matter concentrations.
 *
 * @param out_pm1 Pointer to store PM1.0 in µg/m³
 * @param out_pm25 Pointer to store PM2.5 in µg/m³
 * @param out_pm10 Pointer to store PM10 in µg/m³
 * @return ESP_OK on success
 */
esp_err_t sensor_sim_read_pm(float *out_pm1, float *out_pm25, float *out_pm10);

#endif /* CONFIG_IAQ_SIMULATION */

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SIM_H */
