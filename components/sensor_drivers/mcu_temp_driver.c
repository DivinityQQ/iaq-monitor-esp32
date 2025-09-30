/* components/sensor_drivers/mcu_temp_driver.c */
#include "mcu_temp_driver.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"

static const char *TAG = "MCU_TEMP";

static temperature_sensor_handle_t s_handle = NULL;

esp_err_t mcu_temp_driver_init(float range_min_c, float range_max_c)
{
    if (s_handle) {
        return ESP_OK;
    }
    temperature_sensor_config_t cfg = {
        .range_min = range_min_c,
        .range_max = range_max_c,
    };
    esp_err_t ret = temperature_sensor_install(&cfg, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "install failed: %s", esp_err_to_name(ret));
        s_handle = NULL;
        return ret;
    }
    return ESP_OK;
}

esp_err_t mcu_temp_driver_enable(void)
{
    if (!s_handle) return ESP_ERR_INVALID_STATE;
    return temperature_sensor_enable(s_handle);
}

esp_err_t mcu_temp_driver_disable(void)
{
    if (!s_handle) return ESP_ERR_INVALID_STATE;
    return temperature_sensor_disable(s_handle);
}

esp_err_t mcu_temp_driver_deinit(void)
{
    if (!s_handle) return ESP_OK;
    esp_err_t ret = temperature_sensor_uninstall(s_handle);
    s_handle = NULL;
    return ret;
}

esp_err_t mcu_temp_driver_read_celsius(float *out_celsius)
{
    if (!out_celsius) return ESP_ERR_INVALID_ARG;
    if (!s_handle) return ESP_ERR_INVALID_STATE;
    return temperature_sensor_get_celsius(s_handle, out_celsius);
}

