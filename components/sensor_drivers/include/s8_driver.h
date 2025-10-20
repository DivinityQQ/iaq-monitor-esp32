/* components/sensor_drivers/include/s8_driver.h */
#ifndef S8_DRIVER_H
#define S8_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  modbus_addr;        /* Current Modbus address */
    uint16_t meter_status;       /* IR1 meter status flags */
    uint16_t co2_ppm;            /* IR4 current CO2 ppm */
    uint32_t serial_number;      /* IR30+IR31 serial number */
    uint16_t abc_period_hours;   /* HR32 ABC period (0=disabled) */
    bool     abc_enabled;        /* Derived from period>0 */
} s8_diag_t;

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

/** Diagnostics and configuration **/

/** Read diagnostic info, serial, and ABC configuration. */
esp_err_t s8_driver_get_diag(s8_diag_t *out);

/** Set ABC period in hours (0 disables ABC). */
esp_err_t s8_driver_set_abc_period(uint16_t hours);

/** Enable/disable ABC; when enabling, use provided period (default 180 if 0). */
esp_err_t s8_driver_set_abc_enabled(bool enable, uint16_t period_hours);

/**
 * Disable the S8 sensor (stub - no hardware sleep mode).
 *
 * @return ESP_OK
 */
esp_err_t s8_driver_disable(void);

/**
 * Enable the S8 sensor (stub - no hardware sleep mode).
 *
 * @return ESP_OK
 */
esp_err_t s8_driver_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* S8_DRIVER_H */
