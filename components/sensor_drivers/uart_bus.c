/* components/sensor_drivers/uart_bus.c */
#include "uart_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UART_BUS";

esp_err_t uart_bus_init(uart_port_t uart_port, int tx_gpio, int rx_gpio,
                        int baud_rate, int rx_buffer_size)
{
    /* Validate RX buffer size: ESP-IDF requires > SOC_UART_FIFO_LEN (128 bytes on ESP32-S3) */
    if (rx_buffer_size <= 128) {
        ESP_LOGE(TAG, "UART%d: RX buffer size must be > 128 bytes (UART FIFO size), got %d",
                 uart_port, rx_buffer_size);
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d param config failed: %s", uart_port, esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(uart_port, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d set pin failed: %s", uart_port, esp_err_to_name(ret));
        return ret;
    }

    /* Install UART driver with RX buffer, no TX buffer, no event queue */
    ret = uart_driver_install(uart_port, rx_buffer_size, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d driver install failed: %s", uart_port, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART%d initialized (TX=%d, RX=%d, baud=%d, rx_buf=%d)",
             uart_port, tx_gpio, rx_gpio, baud_rate, rx_buffer_size);

    return ESP_OK;
}

int uart_bus_write_bytes(uart_port_t uart_port, const uint8_t *data, size_t len)
{
    return uart_write_bytes(uart_port, (const char *)data, len);
}

int uart_bus_read_bytes(uart_port_t uart_port, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return uart_read_bytes(uart_port, buf, len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t uart_bus_flush_rx(uart_port_t uart_port)
{
    return uart_flush_input(uart_port);
}

uint16_t uart_calc_checksum_sum16(const uint8_t *buf, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
}

bool uart_validate_pms5003_frame(const uint8_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    /* PMS5003 frame structure:
     * Byte 0-1:  Start bytes (0x42 0x4D)
     * Byte 2-3:  Frame length (high byte, low byte) = 28 (0x00 0x1C)
     * Byte 4-29: Data fields
     * Byte 30-31: Checksum (sum of bytes 0-29)
     */
    if (frame[0] != 0x42 || frame[1] != 0x4D) {
        ESP_LOGW(TAG, "PMS5003: Invalid start bytes (0x%02X 0x%02X)", frame[0], frame[1]);
        return false;
    }

    uint16_t frame_len = (frame[2] << 8) | frame[3];
    if (frame_len != 28) {
        ESP_LOGW(TAG, "PMS5003: Invalid frame length (%u, expected 28)", frame_len);
        return false;
    }

    /* Calculate checksum of bytes 0-29 */
    uint16_t calc_checksum = uart_calc_checksum_sum16(frame, 30);
    uint16_t frame_checksum = (frame[30] << 8) | frame[31];

    if (calc_checksum != frame_checksum) {
        ESP_LOGW(TAG, "PMS5003: Checksum mismatch (calc=0x%04X, frame=0x%04X)",
                 calc_checksum, frame_checksum);
        return false;
    }

    return true;
}

uint16_t uart_calc_crc16_modbus(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

esp_err_t uart_bus_deinit(uart_port_t uart_port)
{
    esp_err_t ret = uart_driver_delete(uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d driver delete failed: %s", uart_port, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART%d deinitialized", uart_port);
    return ESP_OK;
}
