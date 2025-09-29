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

#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "iaq_config.h"

static const char *TAG = "WIFI_MGR";

extern EventGroupHandle_t g_event_group;
extern system_info_t g_system_info;

static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;

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
                g_system_info.wifi_connected = false;
                
                if (s_retry_num < CONFIG_IAQ_MAXIMUM_RETRY) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retry connection to AP (%d/%d)", 
                            s_retry_num, CONFIG_IAQ_MAXIMUM_RETRY);
                } else {
                    xEventGroupSetBits(g_event_group, WIFI_FAIL_BIT);
                    ESP_LOGI(TAG, "Failed to connect to AP");
                }
                xEventGroupClearBits(g_event_group, WIFI_CONNECTED_BIT);
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
                    g_system_info.wifi_connected = true;
                    xEventGroupSetBits(g_event_group, WIFI_CONNECTED_BIT);
                    xEventGroupClearBits(g_event_group, WIFI_FAIL_BIT);

                    // Restart MQTT if it was previously connected
                    if (g_system_info.mqtt_connected && !mqtt_manager_is_connected()) {
                            ESP_LOGI(TAG, "WiFi recovered, restarting MQTT");
                            mqtt_manager_start();
                    }
                }
                break;
                
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGI(TAG, "Lost IP address");
                g_system_info.wifi_connected = false;
                break;
                
            default:
                break;
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi manager");
    
    /* Create default station netif */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    
    /* Initialize WiFi with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_event_handler, NULL));
    
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi in STA mode");
    
    /* Configure WiFi */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_IAQ_WIFI_SSID,
            .password = CONFIG_IAQ_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .failure_retry_cnt = CONFIG_IAQ_MAXIMUM_RETRY,
        },
    };
    
    /* Set WiFi mode and config */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization finished. Connecting to SSID: %s", 
            CONFIG_IAQ_WIFI_SSID);
    
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
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        g_system_info.wifi_rssi = ap_info.rssi;
        return ap_info.rssi;
    }
    return -100;  /* No signal */
}

bool wifi_manager_is_connected(void)
{
    return g_system_info.wifi_connected;
}