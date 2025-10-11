/* components/sensor_drivers/s8_driver.c */
#include "s8_driver.h"
#include "uart_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "S8_DRIVER";

static bool s_initialized = false;
static uart_port_t s_uart_port = (uart_port_t)CONFIG_IAQ_S8_UART_PORT;
static QueueHandle_t s_uart_queue = NULL;

/* Request/response implemented inline in read_co2() using UART event queue */

esp_err_t s8_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "S8 driver already initialized");
        return ESP_OK;
    }

    uart_port_t uart_port = s_uart_port;
    int tx_gpio = CONFIG_IAQ_S8_TX_GPIO;
    int rx_gpio = CONFIG_IAQ_S8_RX_GPIO;
    int rx_buf_size = CONFIG_IAQ_S8_RX_BUF_SIZE;

    esp_err_t ret = uart_bus_init_with_queue(uart_port, tx_gpio, rx_gpio, 9600, rx_buf_size, 8, &s_uart_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART for S8: %s", esp_err_to_name(ret));
        return ret;
    }
    /* Optimize event-driven wakeups: 7-byte Modbus response */
    (void)uart_set_rx_full_threshold(s_uart_port, 7);
    (void)uart_set_rx_timeout(s_uart_port, 2);

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
    /* Flush RX to avoid mixing previous responses */
    (void)uart_bus_flush_rx(s_uart_port);

    /* Write request */
    uint8_t req[8] = {0xFE, 0x04, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00};
    uint16_t crc = uart_calc_crc16_modbus(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)((crc >> 8) & 0xFF);
    int written = uart_bus_write_bytes(s_uart_port, req, sizeof(req));
    if (written != sizeof(req)) {
        *out_co2_ppm = NAN;
        return ESP_FAIL;
    }

    /* Wait for response using event queue */
    uart_event_t ev;
    int64_t start = esp_timer_get_time();
    uint8_t rsp[7];
    int total = 0;
    esp_err_t ret = ESP_ERR_TIMEOUT;
    while ((esp_timer_get_time() - start) / 1000 < 250) {
        if (xQueueReceive(s_uart_queue, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (ev.type == UART_DATA || ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
                size_t available = 0;
                (void)uart_get_buffered_data_len(s_uart_port, &available);
                while (available > 0 && total < (int)sizeof(rsp)) {
                    int to_read = (available < (size_t)(sizeof(rsp) - total)) ? (int)available : (int)(sizeof(rsp) - total);
                    int n = uart_bus_read_bytes(s_uart_port, rsp + total, to_read, 0);
                    if (n <= 0) break;
                    total += n;
                    available -= n;
                }
                if (total >= 7) {
                    /* Validate */
                    if (rsp[0] == 0xFE && rsp[1] == 0x04 && rsp[2] == 0x02) {
                        uint16_t crc_calc = uart_calc_crc16_modbus(rsp, 5);
                        uint16_t crc_frame = (uint16_t)rsp[6] << 8 | rsp[5];
                        if (crc_calc == crc_frame) {
                            uint16_t ppm = ((uint16_t)rsp[3] << 8) | rsp[4];
                            *out_co2_ppm = (float)ppm;
                            return ESP_OK;
                        }
                    }
                    break; /* invalid */
                }
            }
        }
    }
    *out_co2_ppm = NAN;
    return ret;
#endif
}

esp_err_t s8_driver_calibrate_co2(int target_ppm)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Calibration command varies by firmware; keep as stub to avoid unintended writes. */
    (void)target_ppm;
    ESP_LOGW(TAG, "S8 calibrate_co2: not implemented (to avoid unsafe writes)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t s8_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Flush RX buffer to clear any stale data */
    (void)uart_bus_flush_rx(s_uart_port);

    ESP_LOGI(TAG, "S8 driver reset (RX buffer flushed)");
    return ESP_OK;
}

esp_err_t s8_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = uart_bus_deinit(s_uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize UART: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "S8 driver deinitialized");
    return ESP_OK;
}
