/* components/sensor_drivers/include/mcu_temp_driver.h */
#ifndef MCU_TEMP_DRIVER_H
#define MCU_TEMP_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the internal MCU temperature sensor.
 * range_min_c/range_max_c define the measurement range, e.g., -10..80.
 */
esp_err_t mcu_temp_driver_init(float range_min_c, float range_max_c);

/* Enable the temperature sensor (starts conversions). */
esp_err_t mcu_temp_driver_enable(void);

/* Disable the temperature sensor. */
esp_err_t mcu_temp_driver_disable(void);

/* Uninstall the temperature sensor and release resources. */
esp_err_t mcu_temp_driver_deinit(void);

/* Read current MCU temperature in Celsius. */
esp_err_t mcu_temp_driver_read_celsius(float *out_celsius);

#ifdef __cplusplus
}
#endif

#endif /* MCU_TEMP_DRIVER_H */

