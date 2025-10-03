/* components/sensor_drivers/sht41_driver.c */
#include "sht41_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "SHT41_DRIVER";

static bool s_initialized = false;

esp_err_t sht41_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "SHT41 driver already initialized");
        return ESP_OK;
    }

    /* I2C bus should already be initialized by sensor_coordinator */
    /* In real implementation, we would probe for device at 0x44 or 0x45 */

    s_initialized = true;
    ESP_LOGI(TAG, "SHT41 temperature/humidity sensor driver initialized");
    return ESP_OK;
}

esp_err_t sht41_driver_read(float *out_temp_c, float *out_humidity_rh)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "SHT41 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_IAQ_SIMULATION
    esp_err_t ret = ESP_OK;
    if (out_temp_c) {
        ret = sensor_sim_read_temperature(out_temp_c);
        if (ret != ESP_OK) return ret;
    }
    if (out_humidity_rh) {
        ret = sensor_sim_read_humidity(out_humidity_rh);
    }
    return ret;
#else
    /* Stub: real implementation will use I2C to read sensor */
    /* SHT41 I2C address: 0x44 (or 0x45) */
    /* Command sequence: send 0xFD (high precision), wait 8.3ms, read 6 bytes */
    /* Temperature: T = -45 + 175 * (raw_temp / 65535) */
    /* Humidity: RH = -6 + 125 * (raw_hum / 65535) */

    if (out_temp_c) *out_temp_c = NAN;
    if (out_humidity_rh) *out_humidity_rh = NAN;

    ESP_LOGW(TAG, "SHT41 read: stub implementation, returning NAN");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sht41_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "SHT41 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stub: real implementation would send soft reset command (0x94) via I2C */
    ESP_LOGI(TAG, "SHT41 driver reset (stub)");
    return ESP_OK;
}

esp_err_t sht41_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "SHT41 driver deinitialized");
    return ESP_OK;
}
