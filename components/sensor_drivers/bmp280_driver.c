/* components/sensor_drivers/bmp280_driver.c */
#include "bmp280_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "BMP280_DRIVER";

static bool s_initialized = false;

esp_err_t bmp280_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "BMP280 driver already initialized");
        return ESP_OK;
    }

    /* I2C bus should already be initialized by sensor_coordinator */
    /* In real implementation, we would probe for device at 0x76 or 0x77 */

    s_initialized = true;
    ESP_LOGI(TAG, "BMP280 pressure/temperature sensor driver initialized");
    return ESP_OK;
}

esp_err_t bmp280_driver_read(float *out_pressure_hpa, float *out_temp_c)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "BMP280 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_IAQ_SIMULATION
    esp_err_t ret = ESP_OK;
    if (out_pressure_hpa) {
        ret = sensor_sim_read_pressure(out_pressure_hpa);
        if (ret != ESP_OK) return ret;
    }
    if (out_temp_c) {
        /* BMP280 also provides temperature, but we use SHT41 as primary */
        ret = sensor_sim_read_temperature(out_temp_c);
    }
    return ret;
#else
    /* Stub: real implementation will use I2C to read sensor */
    /* BMP280 I2C address: 0x76 (or 0x77 with SDO pulled high) */
    /* Read calibration data, then read raw pressure and temperature */
    /* Apply compensation formulas from datasheet */

    if (out_pressure_hpa) *out_pressure_hpa = NAN;
    if (out_temp_c) *out_temp_c = NAN;

    ESP_LOGW(TAG, "BMP280 read: stub implementation, returning NAN");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bmp280_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "BMP280 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stub: real implementation would send soft reset command (0xE0 = 0xB6) via I2C */
    ESP_LOGI(TAG, "BMP280 driver reset (stub)");
    return ESP_OK;
}

esp_err_t bmp280_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "BMP280 driver deinitialized");
    return ESP_OK;
}
