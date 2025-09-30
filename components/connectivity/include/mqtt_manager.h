/* components/connectivity/include/mqtt_manager.h */
#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "esp_err.h"
#include "iaq_data.h"

/**
 * Initialize the MQTT manager with MQTT 5.0 support.
 * Must be called before mqtt_manager_start().
 *
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t mqtt_manager_init(void);

/**
 * Start the MQTT client (connects to broker).
 * Creates internal MQTT task managed by esp-mqtt component.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_manager_start(void);

/**
 * Stop the MQTT client.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_manager_stop(void);

/**
 * Publish system status/health data to MQTT.
 *
 * @param data Pointer to iaq_data structure
 * @return ESP_OK on success, ESP_FAIL if not connected or data is NULL
 */
esp_err_t mqtt_publish_status(const iaq_data_t *data);

/**
 * Publish sensor data to MQTT.
 *
 * @param data Pointer to iaq_data structure
 * @return ESP_OK on success, ESP_FAIL if not connected or data is NULL
 */
esp_err_t mqtt_publish_sensor_data(const iaq_data_t *data);

/**
 * Check if MQTT client is connected to broker.
 *
 * @return true if connected, false otherwise
 */
bool mqtt_manager_is_connected(void);

/**
 * Set MQTT broker configuration and save to NVS.
 * MQTT client will need to be restarted for changes to take effect.
 *
 * @param broker_url MQTT broker URL (e.g., "mqtt://192.168.1.100:1883")
 * @param username MQTT username (can be NULL)
 * @param password MQTT password (can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_manager_set_broker(const char *broker_url, const char *username, const char *password);

/**
 * Get currently configured MQTT broker URL.
 *
 * @param broker_url Buffer to store broker URL
 * @param url_len Length of the buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_manager_get_broker_url(char *broker_url, size_t url_len);

#endif /* MQTT_MANAGER_H */
