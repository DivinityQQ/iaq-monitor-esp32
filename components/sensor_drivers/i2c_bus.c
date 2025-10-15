/* components/sensor_drivers/i2c_bus.c */
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "I2C_BUS";

static i2c_master_bus_handle_t s_bus_handle = NULL;

esp_err_t i2c_bus_init(void)
{
    if (s_bus_handle != NULL) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_IAQ_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_IAQ_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = CONFIG_IAQ_I2C_INTERNAL_PULLUPS,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(ret));
        s_bus_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d, freq=%d Hz)",
             CONFIG_IAQ_I2C_SDA_GPIO, CONFIG_IAQ_I2C_SCL_GPIO, CONFIG_IAQ_I2C_FREQ_HZ);

    return ESP_OK;
}

esp_err_t i2c_bus_add_device(uint8_t device_addr, uint32_t scl_speed_hz,
                              i2c_master_dev_handle_t *out_handle)
{
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t ret = i2c_master_bus_add_device(s_bus_handle, &dev_config, out_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02X: %s", device_addr, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Added I2C device at 0x%02X (speed=%lu Hz)", device_addr, scl_speed_hz);
    return ESP_OK;
}

esp_err_t i2c_bus_probe(void)
{
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Probing I2C bus...");
    int devices_found = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(s_bus_handle, addr, CONFIG_IAQ_I2C_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            devices_found++;
        }
    }

    ESP_LOGI(TAG, "I2C probe complete: %d device%s found", devices_found, devices_found == 1 ? "" : "s");
    return ESP_OK;
}

esp_err_t i2c_bus_write_read(i2c_master_dev_handle_t dev,
                             const uint8_t *tx, size_t txlen,
                             uint8_t *rx, size_t rxlen,
                             int timeout_ms)
{
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Use IDF v5 I2C master combined transfer API */
    return i2c_master_transmit_receive(dev, tx, txlen, rx, rxlen, timeout_ms);
}

esp_err_t i2c_bus_deinit(void)
{
    if (s_bus_handle == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = i2c_del_master_bus(s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    s_bus_handle = NULL;
    ESP_LOGI(TAG, "I2C bus deinitialized");
    return ESP_OK;
}
