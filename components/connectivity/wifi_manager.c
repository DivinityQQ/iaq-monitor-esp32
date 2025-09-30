/* components/connectivity/wifi_manager.c */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "iaq_data.h"
#include "iaq_config.h"

static const char *TAG = "WIFI_MGR";

static iaq_system_context_t *s_system_ctx = NULL;

/* NVS namespace for WiFi config */
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"

static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;
static bool s_initialized = false;
static char s_ssid[33] = {0};
static char s_password[65] = {0};

/* WiFi event handler */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI(TAG, "WiFi station started, connecting...");
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                /* Update IAQ data */
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_get()->system.wifi_connected = false;
                }

                /* ESP-IDF handles automatic reconnection */
                ESP_LOGI(TAG, "WiFi disconnected, will auto-retry");
                s_retry_num++;
                xEventGroupClearBits(s_system_ctx->event_group, WIFI_CONNECTED_BIT);
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                {
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                    s_retry_num = 0;

                    /* Update IAQ data */
                    bool mqtt_was_connected = false;
                    IAQ_DATA_WITH_LOCK() {
                        iaq_data_t *data = iaq_data_get();
                        data->system.wifi_connected = true;
                        mqtt_was_connected = data->system.mqtt_connected;
                    }

                    xEventGroupSetBits(s_system_ctx->event_group, WIFI_CONNECTED_BIT);

                    /* Restart MQTT if it was previously connected but now disconnected */
                    if (mqtt_was_connected && !mqtt_manager_is_connected()) {
                        ESP_LOGI(TAG, "WiFi recovered, restarting MQTT");
                        mqtt_manager_start();
                    }
                }
                break;
                
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGI(TAG, "Lost IP address");
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_get()->system.wifi_connected = false;
                }
                break;
                
            default:
                break;
        }
    }
}

/* Load WiFi credentials from NVS */
static esp_err_t load_wifi_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    /* Open NVS */
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved WiFi credentials in NVS, using defaults");
        /* Use compile-time defaults */
        strlcpy(s_ssid, CONFIG_IAQ_WIFI_SSID, sizeof(s_ssid));
        strlcpy(s_password, CONFIG_IAQ_WIFI_PASSWORD, sizeof(s_password));
        return ESP_OK;
    }

    /* Read SSID */
    size_t ssid_len = sizeof(s_ssid);
    ret = nvs_get_str(nvs_handle, NVS_KEY_SSID, s_ssid, &ssid_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SSID from NVS: %s", esp_err_to_name(ret));
        strlcpy(s_ssid, CONFIG_IAQ_WIFI_SSID, sizeof(s_ssid));
    }

    /* Read password */
    size_t password_len = sizeof(s_password);
    ret = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_password, &password_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read password from NVS: %s", esp_err_to_name(ret));
        strlcpy(s_password, CONFIG_IAQ_WIFI_PASSWORD, sizeof(s_password));
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Loaded WiFi credentials from NVS: SSID=%s", s_ssid);
    return ESP_OK;
}

/* Save WiFi credentials to NVS */
static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    /* Open NVS */
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Write SSID */
    ret = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write SSID to NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    /* Write password */
    ret = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write password to NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    /* Commit changes */
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Saved WiFi credentials to NVS");
    return ret;
}

esp_err_t wifi_manager_init(iaq_system_context_t *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid system context");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        ESP_LOGW(TAG, "WiFi manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi manager");

    /* Store system context */
    s_system_ctx = ctx;

    /* Create default station netif */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
        return ESP_FAIL;
    }

    /* Initialize WiFi with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register event handlers */
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Load WiFi credentials from NVS */
    load_wifi_credentials();

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting WiFi in STA mode");

    /* Guard: skip if SSID not configured */
    if (s_ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi disabled: SSID not configured. Set credentials via console.");
        return ESP_OK; /* Non-fatal so system remains recoverable */
    }

    /* Configure WiFi with loaded credentials */
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    /* Set WiFi mode and config */
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Note: Automatic reconnection is enabled by default in ESP-IDF */

    /* Start WiFi (this creates internal WiFi task) */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi started, connecting to SSID: %s", s_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi");
    ESP_ERROR_CHECK(esp_wifi_stop());
    return ESP_OK;
}

int32_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    int32_t rssi = -100;  /* No signal */

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    return rssi;
}

bool wifi_manager_is_connected(void)
{
    bool connected = false;

    IAQ_DATA_WITH_LOCK() {
        connected = iaq_data_get()->system.wifi_connected;
    }

    return connected;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_aps, uint16_t *num_aps_found)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ap_records || !num_aps_found || max_aps == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    /* Configure scan */
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    /* Start scan (blocking) */
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Get scan results */
    uint16_t ap_count = max_aps;
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        return ret;
    }

    *num_aps_found = ap_count;
    ESP_LOGI(TAG, "Scan complete, found %d APs", ap_count);

    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid credentials");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ssid) == 0 || strlen(ssid) > 32) {
        ESP_LOGE(TAG, "Invalid SSID length");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(password) > 64) {
        ESP_LOGE(TAG, "Invalid password length");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setting WiFi credentials: SSID=%s", ssid);

    /* Save to NVS */
    esp_err_t ret = save_wifi_credentials(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save credentials to NVS");
        return ret;
    }

    /* Update runtime credentials */
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_password, password, sizeof(s_password));

    ESP_LOGI(TAG, "WiFi credentials updated. Restart WiFi to apply changes.");
    return ESP_OK;
}

esp_err_t wifi_manager_get_ssid(char *ssid, size_t ssid_len)
{
    if (!ssid || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(ssid, s_ssid, ssid_len);
    return ESP_OK;
}
