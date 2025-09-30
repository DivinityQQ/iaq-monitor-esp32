/* components/sensor_drivers/include/s8_driver.h */
#ifndef S8_DRIVER_H
#define S8_DRIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calibrate CO2 sensor to given ppm (stub implementation). */
esp_err_t s8_calibrate_co2(int ppm);

#ifdef __cplusplus
}
#endif

#endif /* S8_DRIVER_H */

