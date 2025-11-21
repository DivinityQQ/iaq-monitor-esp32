/* components/sensor_drivers/sht45_driver.c */
#include "sht45_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pm_guard.h"
#include <math.h>
#include <string.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "SHT4X_DRIVER";

/* SHT4x default I2C address is 0x44 (0x45 for some variants) */
#define SHT4X_I2C_ADDR       0x44

/* Commands */
#define SHT4X_CMD_MEASURE_HP 0xFD  /* High precision measurement */
#define SHT4X_CMD_SOFT_RESET 0x94

/* CRC parameters (Sensirion) */
#define SHT4X_CRC_POLY  0x31
#define SHT4X_CRC_INIT  0xFF

static bool s_initialized = false;
static i2c_master_dev_handle_t s_dev = NULL;

static uint8_t crc8_sensirion(const uint8_t *data, size_t len)
{
    uint8_t crc = SHT4X_CRC_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (crc << 1) ^ SHT4X_CRC_POLY;
            else crc <<= 1;
        }
    }
    return crc;
}

esp_err_t sht45_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "SHT45 driver already initialized");
        return ESP_OK;
    }

    /* I2C bus should already be initialized by sensor_coordinator */
    esp_err_t ret = i2c_bus_add_device(SHT4X_I2C_ADDR, CONFIG_IAQ_I2C_FREQ_HZ, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SHT4x device: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "SHT4x temperature/humidity sensor driver initialized");
    return ESP_OK;
}

esp_err_t sht45_driver_read(float *out_temp_c, float *out_humidity_rh)
{
    if (!s_initialized || s_dev == NULL) {
        ESP_LOGE(TAG, "SHT4x driver not initialized");
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
    /* Issue high-precision measurement */
    pm_guard_lock_no_sleep();
    pm_guard_lock_bus();
    uint8_t cmd = SHT4X_CMD_MEASURE_HP;
    esp_err_t ret = i2c_master_transmit(s_dev, &cmd, 1, CONFIG_IAQ_I2C_TIMEOUT_MS);
    pm_guard_unlock_bus();
    pm_guard_unlock_no_sleep();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT4x transmit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Wait measurement time: typical 8.3ms; add extra slack to cover scheduling jitter */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Read 6 bytes: T[2] CRC, RH[2] CRC */
    uint8_t rx[6] = {0};
    pm_guard_lock_bus();
    ret = i2c_master_receive(s_dev, rx, sizeof(rx), CONFIG_IAQ_I2C_TIMEOUT_MS);
    pm_guard_unlock_bus();
    pm_guard_unlock_no_sleep();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT4x receive failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Validate CRCs */
    if (crc8_sensirion(&rx[0], 2) != rx[2] || crc8_sensirion(&rx[3], 2) != rx[5]) {
        ESP_LOGW(TAG, "SHT4x CRC check failed");
        ret = ESP_ERR_INVALID_CRC;
        return ret;
    }

    uint16_t raw_t = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t raw_rh = ((uint16_t)rx[3] << 8) | rx[4];

    float t_c = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    float rh = -6.0f + 125.0f * ((float)raw_rh / 65535.0f);

    if (out_temp_c) *out_temp_c = t_c;
    if (out_humidity_rh) {
        /* Clamp RH to [0,100] as per datasheet recommendation */
        if (rh < 0.0f) {
            rh = 0.0f;
        }
        if (rh > 100.0f) {
            rh = 100.0f;
        }
        *out_humidity_rh = rh;
    }
    ret = ESP_OK;
    return ret;
#endif
}

esp_err_t sht45_driver_reset(void)
{
    if (!s_initialized || s_dev == NULL) {
        ESP_LOGE(TAG, "SHT4x driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd = SHT4X_CMD_SOFT_RESET;
    pm_guard_lock_bus();
    esp_err_t ret = i2c_master_transmit(s_dev, &cmd, 1, CONFIG_IAQ_I2C_TIMEOUT_MS);
    if (ret == ESP_OK) {
        /* Datasheet suggests up to 1ms after reset */
        vTaskDelay(pdMS_TO_TICKS(2));
        ESP_LOGI(TAG, "SHT4x soft reset issued");
    }
    pm_guard_unlock_bus();
    return ret;
}

esp_err_t sht45_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_dev) {
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    s_initialized = false;
    ESP_LOGI(TAG, "SHT4x driver deinitialized");
    return ESP_OK;
}

esp_err_t sht45_driver_disable(void)
{
    /* Stub implementation - SHT45 has no hardware sleep mode */
    ESP_LOGI(TAG, "SHT45 disabled (no hardware sleep mode)");
    return ESP_OK;
}

esp_err_t sht45_driver_enable(void)
{
    /* Stub implementation - SHT45 has no hardware sleep mode */
    ESP_LOGI(TAG, "SHT45 enabled (no hardware sleep mode)");
    return ESP_OK;
}
