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
#include "iaq_data.h"
#include "iaq_config.h"

static const char *TAG = "WIFI_MGR";

static iaq_system_context_t *s_system_ctx = NULL;

/* NVS namespace for WiFi config */
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_initialized = false;
static char s_ssid[33] = {0};
static char s_password[65] = {0};
static bool s_has_nvs_credentials = false;
static wifi_mode_t s_current_mode = WIFI_MODE_NULL;
static int s_connect_retries = 0;
static bool s_pending_provisioning = false;   /* true after credentials are set until first successful IP */
static bool s_ever_connected = false;         /* persisted across boots */

/* Kconfig defaults for SoftAP */
#ifndef CONFIG_IAQ_AP_SSID
#define CONFIG_IAQ_AP_SSID "IAQ-Setup"
#endif
#ifndef CONFIG_IAQ_AP_PASSWORD
#define CONFIG_IAQ_AP_PASSWORD ""
#endif
#ifndef CONFIG_IAQ_AP_CHANNEL
#define CONFIG_IAQ_AP_CHANNEL 1
#endif
#ifndef CONFIG_IAQ_AP_MAX_CONN
#define CONFIG_IAQ_AP_MAX_CONN 4
#endif
#ifndef CONFIG_IAQ_WIFI_CONNECT_MAX_RETRY
#define CONFIG_IAQ_WIFI_CONNECT_MAX_RETRY 10
#endif

/* Additional NVS keys */
#define NVS_KEY_CONNECTED_ONCE "connected_once"

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

                /* Explicitly trigger reconnect to ensure robustness across IDF configs */
                const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGI(TAG, "WiFi disconnected (reason=%d), reconnecting...", disc ? disc->reason : -1);
                /* Clear connection bit and attempt reconnect */
                xEventGroupClearBits(s_system_ctx->event_group, WIFI_CONNECTED_BIT);
                /* Post WiFi disconnected event to default event loop (non-blocking) */
                esp_event_post(IAQ_EVENT, IAQ_EVENT_WIFI_DISCONNECTED, NULL, 0, 0);
                
                /* Retry behavior depends on whether we're in provisioning phase */
                if (s_current_mode == WIFI_MODE_STA || s_current_mode == WIFI_MODE_APSTA) {
                    if (s_pending_provisioning) {
                        if (s_connect_retries < CONFIG_IAQ_WIFI_CONNECT_MAX_RETRY) {
                            s_connect_retries++;
                            esp_wifi_connect();
                        } else {
                            ESP_LOGW(TAG, "Provisioning connect failed after %d retries; starting SoftAP for re-entry", CONFIG_IAQ_WIFI_CONNECT_MAX_RETRY);
                            (void)wifi_manager_start_ap();
                        }
                    } else {
                        /* Normal operation: keep retrying indefinitely, do not fall back to AP */
                        esp_wifi_connect();
                    }
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "SoftAP started (SSID=%s, channel=%d)", CONFIG_IAQ_AP_SSID, CONFIG_IAQ_AP_CHANNEL);
                break;
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "SoftAP stopped");
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

                    /* Update IAQ data */
                    IAQ_DATA_WITH_LOCK() {
                        iaq_data_get()->system.wifi_connected = true;
                    }

                    xEventGroupSetBits(s_system_ctx->event_group, WIFI_CONNECTED_BIT);

                    /* Post WiFi connected event to default event loop (non-blocking) */
                    esp_event_post(IAQ_EVENT, IAQ_EVENT_WIFI_CONNECTED, NULL, 0, 0);

                    /* Reset retry counter on success */
                    s_connect_retries = 0;

                    /* Mark first success after last credential change and persist */
                    if (s_pending_provisioning || !s_ever_connected) {
                        s_pending_provisioning = false;
                        if (!s_ever_connected) {
                            s_ever_connected = true;
                            nvs_handle_t h;
                            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_u8(h, NVS_KEY_CONNECTED_ONCE, 1);
                                nvs_commit(h);
                                nvs_close(h);
                            }
                        }
                    }
                }
                break;
                
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGI(TAG, "Lost IP address");
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_get()->system.wifi_connected = false;
                }
                /* Keep event bit state consistent with IAQ data and notify */
                xEventGroupClearBits(s_system_ctx->event_group, WIFI_CONNECTED_BIT);
                esp_event_post(IAQ_EVENT, IAQ_EVENT_WIFI_DISCONNECTED, NULL, 0, 0);
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
        s_has_nvs_credentials = false;
        return ESP_OK;
    }

    /* Read SSID */
    size_t ssid_len = sizeof(s_ssid);
    ret = nvs_get_str(nvs_handle, NVS_KEY_SSID, s_ssid, &ssid_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SSID from NVS: %s", esp_err_to_name(ret));
        strlcpy(s_ssid, CONFIG_IAQ_WIFI_SSID, sizeof(s_ssid));
        s_has_nvs_credentials = false;
    }

    /* Read password */
    size_t password_len = sizeof(s_password);
    ret = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_password, &password_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read password from NVS: %s", esp_err_to_name(ret));
        strlcpy(s_password, CONFIG_IAQ_WIFI_PASSWORD, sizeof(s_password));
        s_has_nvs_credentials = false;
    }

    /* Read ever-connected flag (optional) */
    uint8_t connected_once_u8 = 0;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_CONNECTED_ONCE, &connected_once_u8);
    if (ret == ESP_OK) {
        s_ever_connected = (connected_once_u8 != 0);
    } else {
        s_ever_connected = false;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Loaded WiFi credentials from NVS: SSID=%s (ever_connected=%s)", s_ssid, s_ever_connected ? "yes" : "no");
    s_has_nvs_credentials = true;
    s_pending_provisioning = false;
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

    /* Reset connected_once flag until we confirm a successful IP */
    esp_err_t r2 = nvs_set_u8(nvs_handle, NVS_KEY_CONNECTED_ONCE, 0);
    if (r2 != ESP_OK) {
        ESP_LOGW(TAG, "Failed to reset connected_once: %s", esp_err_to_name(r2));
    } else {
        s_ever_connected = false;
    }

    /* Commit changes */
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Saved WiFi credentials to NVS (connected_once reset)");
    s_has_nvs_credentials = true;
    s_pending_provisioning = true;
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

    /* Create default station + softAP netifs (safe even if unused initially) */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
        return ESP_FAIL;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi AP interface");
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

    /* Auto-select mode: AP for provisioning if no NVS creds, otherwise STA or APSTA */
    if (!s_has_nvs_credentials) {
        ESP_LOGW(TAG, "No NVS WiFi credentials. Starting SoftAP for provisioning.");
        return wifi_manager_start_ap();
    }

    return wifi_manager_start_sta();
}

esp_err_t wifi_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi");
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_start_sta(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    if (s_ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi STA disabled: SSID not configured.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting WiFi in STA%s mode", (s_current_mode == WIFI_MODE_APSTA ? "+AP" : ""));

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, s_ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, s_password, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    /* Decide whether to keep AP (APSTA) based on Kconfig */
#ifdef CONFIG_IAQ_AP_KEEP_AFTER_PROVISION
    bool keep_ap = CONFIG_IAQ_AP_KEEP_AFTER_PROVISION;
#else
    bool keep_ap = false;
#endif

    wifi_mode_t target = keep_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA;

    /* Ensure a clean start */
    (void) esp_wifi_stop();

    esp_err_t ret = esp_wifi_set_mode(target);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        return ret;
    }

    /* If APSTA, ensure AP side has config too */
    if (target == WIFI_MODE_APSTA) {
        wifi_config_t ap_cfg = {0};
        strlcpy((char *)ap_cfg.ap.ssid, CONFIG_IAQ_AP_SSID, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = strlen(CONFIG_IAQ_AP_SSID);
        strlcpy((char *)ap_cfg.ap.password, CONFIG_IAQ_AP_PASSWORD, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.channel = CONFIG_IAQ_AP_CHANNEL;
        ap_cfg.ap.max_connection = CONFIG_IAQ_AP_MAX_CONN;
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
        ap_cfg.ap.authmode = WIFI_AUTH_WPA3_PSK;
        ap_cfg.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#else
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
#endif
        ap_cfg.ap.pmf_cfg.required = true;
        size_t ap_pass_len2 = strlen(CONFIG_IAQ_AP_PASSWORD);
        if (ap_pass_len2 == 0) {
            ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
            ap_cfg.ap.password[0] = '\0';
            ap_cfg.ap.pmf_cfg.required = false;
        } else if (ap_pass_len2 < 8) {
            ESP_LOGW(TAG, "SoftAP password < 8 chars; using OPEN AP (AP+STA)");
            ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
            ap_cfg.ap.password[0] = '\0';
            ap_cfg.ap.pmf_cfg.required = false;
        }
        ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    /* Start WiFi */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    s_current_mode = target;
    s_connect_retries = 0;
    ESP_LOGI(TAG, "WiFi STA started. SSID: %s", s_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    wifi_config_t ap_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, CONFIG_IAQ_AP_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(CONFIG_IAQ_AP_SSID);
    strlcpy((char *)ap_cfg.ap.password, CONFIG_IAQ_AP_PASSWORD, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = CONFIG_IAQ_AP_CHANNEL;
    ap_cfg.ap.max_connection = CONFIG_IAQ_AP_MAX_CONN;
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
    ap_cfg.ap.authmode = WIFI_AUTH_WPA3_PSK;
    ap_cfg.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#else
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
#endif
    ap_cfg.ap.pmf_cfg.required = true;
    size_t ap_pass_len = strlen(CONFIG_IAQ_AP_PASSWORD);
    if (ap_pass_len == 0) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ap_cfg.ap.password[0] = '\0';
        ap_cfg.ap.pmf_cfg.required = false;
    } else if (ap_pass_len < 8) {
        ESP_LOGW(TAG, "SoftAP password < 8 chars; starting OPEN AP for provisioning");
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ap_cfg.ap.password[0] = '\0';
        ap_cfg.ap.pmf_cfg.required = false;
    }

    /* Ensure a clean start */
    (void) esp_wifi_stop();
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(ret));
        return ret;
    }
    s_current_mode = WIFI_MODE_AP;
    bool open_ap = (ap_cfg.ap.authmode == WIFI_AUTH_OPEN);
    if (open_ap) {
        ESP_LOGI(TAG, "SoftAP active. SSID:%s (open) ch:%d", CONFIG_IAQ_AP_SSID, CONFIG_IAQ_AP_CHANNEL);
    } else {
        ESP_LOGI(TAG, "SoftAP active. SSID:%s (secured) ch:%d", CONFIG_IAQ_AP_SSID, CONFIG_IAQ_AP_CHANNEL);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_current_mode != WIFI_MODE_AP && s_current_mode != WIFI_MODE_APSTA) {
        return ESP_OK;
    }
    /* If APSTA, switch to STA. If AP only, stop WiFi entirely. */
    if (s_current_mode == WIFI_MODE_APSTA) {
        esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) return ret;
        s_current_mode = WIFI_MODE_STA;
        return ESP_OK;
    }
    return esp_wifi_stop();
}

bool wifi_manager_is_provisioned(void)
{
    return s_has_nvs_credentials;
}

wifi_mode_t wifi_manager_get_mode(void)
{
    return s_current_mode;
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

    /* Scanning is not supported in AP-only mode in ESP-IDF */
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) mode = WIFI_MODE_NULL;
    if (mode == WIFI_MODE_AP) {
        ESP_LOGW(TAG, "WiFi scan is not supported in AP-only mode; use STA or AP+STA");
        return ESP_ERR_NOT_SUPPORTED;
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

bool wifi_manager_is_configured(void)
{
    return (s_ssid[0] != '\0');
}
