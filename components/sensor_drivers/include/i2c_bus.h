/* components/sensor_drivers/include/i2c_bus.h */
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the shared I2C master bus.
 * Uses Kconfig settings for SDA/SCL pins and frequency.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_init(void);

/**
 * Add an I2C device to the bus.
 *
 * @param device_addr 7-bit I2C device address
 * @param scl_speed_hz Clock speed for this device (typically 100kHz or 400kHz)
 * @param out_handle Pointer to receive device handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_add_device(uint8_t device_addr, uint32_t scl_speed_hz,
                              i2c_master_dev_handle_t *out_handle);

/**
 * Probe for I2C devices on the bus.
 * Scans all 7-bit addresses (0x08-0x77) and logs found devices.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_probe(void);

/**
 * Deinitialize the I2C bus and release resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */
