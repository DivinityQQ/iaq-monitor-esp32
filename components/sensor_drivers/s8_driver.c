/* components/sensor_drivers/s8_driver.c */
#include "s8_driver.h"
#include "uart_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "S8_DRIVER";

static bool s_initialized = false;

esp_err_t s8_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "S8 driver already initialized");
        return ESP_OK;
    }

    uart_port_t uart_port = (uart_port_t)CONFIG_IAQ_S8_UART_PORT;
    int tx_gpio = CONFIG_IAQ_S8_TX_GPIO;
    int rx_gpio = CONFIG_IAQ_S8_RX_GPIO;
    int rx_buf_size = CONFIG_IAQ_S8_RX_BUF_SIZE;

    esp_err_t ret = uart_bus_init(uart_port, tx_gpio, rx_gpio, 9600, rx_buf_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART for S8: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "S8 CO2 sensor driver initialized (UART%d)", uart_port);
    return ESP_OK;
}

esp_err_t s8_driver_read_co2(float *out_co2_ppm)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (out_co2_ppm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef CONFIG_IAQ_SIMULATION
    return sensor_sim_read_co2(out_co2_ppm);
#else
    /* Stub: real implementation will use Modbus protocol to read CO2 */
    /* Example Modbus command: FE 04 00 03 00 01 D5 C5 (read input register 3) */
    /* Response: FE 04 02 <CO2_HIGH> <CO2_LOW> <CRC_LOW> <CRC_HIGH> */

    ESP_LOGW(TAG, "S8 read_co2: stub implementation, returning NAN");
    *out_co2_ppm = NAN;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t s8_driver_calibrate_co2(int target_ppm)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stub: real implementation will send Modbus calibration command */
    /* Example: FE 06 00 01 <PPM_HIGH> <PPM_LOW> <CRC_LOW> <CRC_HIGH> */

    (void)target_ppm;
    ESP_LOGW(TAG, "S8 calibrate_co2: stub implementation, target=%d ppm", target_ppm);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t s8_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Flush RX buffer to clear any stale data */
    uart_port_t uart_port = (uart_port_t)CONFIG_IAQ_S8_UART_PORT;
    uart_bus_flush_rx(uart_port);

    ESP_LOGI(TAG, "S8 driver reset (RX buffer flushed)");
    return ESP_OK;
}

esp_err_t s8_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    uart_port_t uart_port = (uart_port_t)CONFIG_IAQ_S8_UART_PORT;
    esp_err_t ret = uart_bus_deinit(uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize UART: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "S8 driver deinitialized");
    return ESP_OK;
}
