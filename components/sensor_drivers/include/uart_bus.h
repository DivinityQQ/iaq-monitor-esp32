/* components/sensor_drivers/include/uart_bus.h */
#ifndef UART_BUS_H
#define UART_BUS_H

#include "esp_err.h"
#include "driver/uart.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize a UART port with specified pins and baud rate.
 *
 * @param uart_port UART port number (UART_NUM_0, UART_NUM_1, UART_NUM_2)
 * @param tx_gpio GPIO number for TX pin
 * @param rx_gpio GPIO number for RX pin
 * @param baud_rate Baud rate (e.g., 9600 for PMS5003 and S8)
 * @param rx_buffer_size RX ring buffer size in bytes
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t uart_bus_init(uart_port_t uart_port, int tx_gpio, int rx_gpio,
                        int baud_rate, int rx_buffer_size);

/**
 * Write bytes to UART.
 *
 * @param uart_port UART port number
 * @param data Pointer to data buffer
 * @param len Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
int uart_bus_write_bytes(uart_port_t uart_port, const uint8_t *data, size_t len);

/**
 * Read bytes from UART with timeout.
 *
 * @param uart_port UART port number
 * @param buf Buffer to store read data
 * @param len Maximum number of bytes to read
 * @param timeout_ms Read timeout in milliseconds
 * @return Number of bytes read, or -1 on error
 */
int uart_bus_read_bytes(uart_port_t uart_port, uint8_t *buf, size_t len, uint32_t timeout_ms);

/**
 * Flush UART RX buffer (discard any pending data).
 *
 * @param uart_port UART port number
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t uart_bus_flush_rx(uart_port_t uart_port);

/**
 * Calculate 16-bit checksum (sum of bytes) for PMS5003 protocol.
 *
 * @param buf Data buffer
 * @param len Number of bytes to checksum
 * @return 16-bit checksum value
 */
uint16_t uart_calc_checksum_sum16(const uint8_t *buf, size_t len);

/**
 * Validate PMS5003 frame structure and checksum.
 *
 * @param frame Pointer to 32-byte PMS5003 frame
 * @return true if frame is valid, false otherwise
 */
bool uart_validate_pms5003_frame(const uint8_t *frame);

/**
 * Calculate Modbus CRC16 for Senseair S8 protocol.
 *
 * @param buf Data buffer
 * @param len Number of bytes to checksum
 * @return 16-bit CRC value
 */
uint16_t uart_calc_crc16_modbus(const uint8_t *buf, size_t len);

/**
 * Deinitialize a UART port.
 *
 * @param uart_port UART port number
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t uart_bus_deinit(uart_port_t uart_port);

#ifdef __cplusplus
}
#endif

#endif /* UART_BUS_H */
