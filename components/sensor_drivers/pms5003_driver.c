/* components/sensor_drivers/pms5003_driver.c */
#include "pms5003_driver.h"
#include "uart_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "PMS5003_DRIVER";

static bool s_initialized = false;

esp_err_t pms5003_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "PMS5003 driver already initialized");
        return ESP_OK;
    }

    uart_port_t uart_port = (uart_port_t)CONFIG_IAQ_PMS5003_UART_PORT;
    int tx_gpio = CONFIG_IAQ_PMS5003_TX_GPIO;
    int rx_gpio = CONFIG_IAQ_PMS5003_RX_GPIO;
    int rx_buf_size = CONFIG_IAQ_PMS5003_RX_BUF_SIZE;

    esp_err_t ret = uart_bus_init(uart_port, tx_gpio, rx_gpio, 9600, rx_buf_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART for PMS5003: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "PMS5003 particulate matter sensor driver initialized (UART%d)", uart_port);
    return ESP_OK;
}

esp_err_t pms5003_driver_read(float *out_pm1_0, float *out_pm2_5, float *out_pm10)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "PMS5003 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_IAQ_SIMULATION
    return sensor_sim_read_pm(out_pm1_0, out_pm2_5, out_pm10);
#else
    /* Stub: real implementation will use UART to read sensor */
    /* PMS5003 protocol:
     * - 32-byte frame starting with 0x42 0x4d
     * - Frame: header(2) + length(2) + PM1.0(2) + PM2.5(2) + PM10(2) + ... + checksum(2)
     * - Send wakeup command before first read: 0x42 0x4d 0xe4 0x00 0x01 0x01 0x74
     * - Parse frame and extract CF=1 PM values (atmospheric environment)
     */

    if (out_pm1_0) *out_pm1_0 = NAN;
    if (out_pm2_5) *out_pm2_5 = NAN;
    if (out_pm10) *out_pm10 = NAN;

    ESP_LOGW(TAG, "PMS5003 read: stub implementation, returning NAN");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t pms5003_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "PMS5003 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Flush RX buffer to clear any stale data */
    uart_port_t uart_port = (uart_port_t)CONFIG_IAQ_PMS5003_UART_PORT;
    uart_bus_flush_rx(uart_port);

    /* Stub: real implementation would send wake command: 0x42 0x4d 0xe4 0x00 0x01 0x01 0x74 */
    ESP_LOGI(TAG, "PMS5003 driver reset (RX buffer flushed)");
    return ESP_OK;
}

esp_err_t pms5003_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    uart_port_t uart_port = (uart_port_t)CONFIG_IAQ_PMS5003_UART_PORT;
    esp_err_t ret = uart_bus_deinit(uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize UART: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "PMS5003 driver deinitialized");
    return ESP_OK;
}
