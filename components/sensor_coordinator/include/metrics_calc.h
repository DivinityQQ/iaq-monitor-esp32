/* components/sensor_coordinator/include/metrics_calc.h */
#ifndef METRICS_CALC_H
#define METRICS_CALC_H

#include "esp_err.h"
#include "iaq_data.h"

/**
 * Initialize metrics calculation subsystem.
 * Initializes ring buffers for trend tracking (pressure, CO2 rate, PM spike).
 *
 * @return ESP_OK on success
 */
esp_err_t metrics_init(void);

/**
 * Calculate all derived metrics from fused sensor data.
 *
 * This function calculates (in order):
 * 1. EPA AQI (PM2.5/PM10)
 * 2. Thermal comfort (dew point, heat index, comfort score)
 * 3. Air quality scores (CO2, overall IAQ)
 * 4. Pressure trend (RISING/STABLE/FALLING)
 * 5. CO2 rate of change (ppm/hr)
 * 6. PM2.5 spike detection
 * 7. Mold risk index
 * 8. VOC/NOx categories
 *
 * Results are written to data->metrics.
 * Each metric is conditional on Kconfig enable flags and valid input data.
 *
 * @param data Pointer to locked iaq_data structure (caller must hold mutex)
 */
void metrics_calculate_all(iaq_data_t *data);

#endif /* METRICS_CALC_H */
