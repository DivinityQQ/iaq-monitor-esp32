/* components/connectivity/include/wifi_manager.h */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "system_context.h"

/**
 * Initialize the WiFi manager.
 * Loads credentials from NVS if available.
 *
 * @param ctx System context containing event group for synchronization
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(iaq_system_context_t *ctx);

/**
 * Start WiFi and connect to configured network.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_start(void);

/**
 * Stop WiFi.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_stop(void);

/**
 * Get current WiFi RSSI (signal strength).
 *
 * @return RSSI in dBm, or -100 if not connected
 */
int32_t wifi_manager_get_rssi(void);

/**
 * Check if WiFi is connected.
 *
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * Scan for available WiFi networks.
 * Blocks until scan is complete.
 *
 * @param ap_records Array to store AP records
 * @param max_aps Maximum number of APs to store
 * @param num_aps_found Number of APs actually found
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_aps, uint16_t *num_aps_found);

/**
 * Set WiFi credentials and save to NVS.
 * WiFi will need to be restarted for changes to take effect.
 *
 * @param ssid WiFi SSID (max 32 chars)
 * @param password WiFi password (max 64 chars)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/**
 * Get currently configured SSID.
 *
 * @param ssid Buffer to store SSID (should be at least 33 bytes)
 * @param ssid_len Length of the buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_ssid(char *ssid, size_t ssid_len);

/**
 * Check if WiFi is configured with valid credentials.
 *
 * @return true if SSID is configured, false otherwise
 */
bool wifi_manager_is_configured(void);

#endif /* WIFI_MANAGER_H */