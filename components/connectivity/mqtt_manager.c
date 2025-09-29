/* components/connectivity/mqtt_manager.c */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "mqtt_manager.h"
#include "iaq_config.h"

static const char *TAG = "MQTT_MGR";

extern EventGroupHandle_t g_event_group;
extern system_info_t g_system_info;

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

/* Topic definitions */
#define TOPIC_STATUS    CONFIG_IAQ_DEVICE_ID "/status"
#define TOPIC_STATE     CONFIG_IAQ_DEVICE_ID "/state"
#define TOPIC_COMMAND   CONFIG_IAQ_DEVICE_ID "/cmd/+"
#define TOPIC_LWT       CONFIG_IAQ_DEVICE_ID "/availability"

/* Publish Home Assistant discovery message */
static void mqtt_publish_ha_discovery(void)
{
    /* Example for temperature sensor */
    cJSON *root = cJSON_CreateObject();
    cJSON *device = cJSON_CreateObject();
    
    /* Device information */
    cJSON_AddStringToObject(device, "identifiers", CONFIG_IAQ_DEVICE_ID);
    cJSON_AddStringToObject(device, "name", "IAQ Monitor");
    cJSON_AddStringToObject(device, "model", "ESP32-S3");
    cJSON_AddStringToObject(device, "manufacturer", "DIY");
    char sw_version[32];
    snprintf(sw_version, sizeof(sw_version), "%d.%d.%d", 
             IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);
    cJSON_AddStringToObject(device, "sw_version", sw_version);
    
    /* Sensor configuration */
    cJSON_AddStringToObject(root, "name", "Temperature");
    cJSON_AddStringToObject(root, "state_topic", CONFIG_IAQ_DEVICE_ID "/state");
    cJSON_AddStringToObject(root, "availability_topic", TOPIC_LWT);
    cJSON_AddStringToObject(root, "device_class", "temperature");
    cJSON_AddStringToObject(root, "unit_of_measurement", "°C");
    cJSON_AddStringToObject(root, "value_template", "{{ value_json.temperature }}");
    cJSON_AddStringToObject(root, "unique_id", CONFIG_IAQ_DEVICE_ID "_temp");
    cJSON_AddItemToObject(root, "device", device);
    
    char *json_string = cJSON_Print(root);
    if (json_string) {
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_temp/config", CONFIG_IAQ_DEVICE_ID);
        esp_mqtt_client_publish(s_mqtt_client, topic, json_string, 0, 1, true);
        ESP_LOGI(TAG, "Published HA discovery to %s", topic);
        free(json_string);
    }
    
    cJSON_Delete(root);
}

/* MQTT event handler */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_mqtt_connected = true;
            g_system_info.mqtt_connected = true;
            xEventGroupSetBits(g_event_group, MQTT_CONNECTED_BIT);
            
            /* Subscribe to command topic */
            int msg_id = esp_mqtt_client_subscribe(client, TOPIC_COMMAND, CONFIG_IAQ_MQTT_QOS);
            ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", TOPIC_COMMAND, msg_id);
            
            /* Publish online status */
            esp_mqtt_client_publish(client, TOPIC_LWT, "online", 0, 1, true);
            
            /* Send Home Assistant discovery (basic example) */
            mqtt_publish_ha_discovery();
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            g_system_info.mqtt_connected = false;
            xEventGroupClearBits(g_event_group, MQTT_CONNECTED_BIT);
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received");
            ESP_LOGI(TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);
            /* Handle commands here */
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls error: 0x%x", event->error_handle->esp_tls_stack_err);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}

esp_err_t mqtt_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT client");
    
    /* Configure MQTT client */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_IAQ_MQTT_BROKER_URL,
        .credentials = {
            .client_id = CONFIG_IAQ_DEVICE_ID,
            .username = CONFIG_IAQ_MQTT_USERNAME,
            .authentication = {
                .password = CONFIG_IAQ_MQTT_PASSWORD
            }
        },
        .session = {
            .last_will = {
                .topic = TOPIC_LWT,
                .msg = "offline",
                .qos = 1,
                .retain = true
            },
            .keepalive = 60,
            .disable_clean_session = false,
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        },
        .buffer.size = 1024,
    };
    
    /* Create MQTT client */
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    /* Register event handler */
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, 
                                                    mqtt_event_handler, NULL));
    
    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    ESP_LOGI(TAG, "Starting MQTT client");
    return esp_mqtt_client_start(s_mqtt_client);
}

esp_err_t mqtt_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping MQTT client");
    return esp_mqtt_client_stop(s_mqtt_client);
}

esp_err_t mqtt_publish_status(const system_info_t *info)
{
    if (!s_mqtt_connected || !info) {
        return ESP_FAIL;
    }
    
    /* Create JSON status */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime", info->uptime_seconds);
    cJSON_AddNumberToObject(root, "free_heap", info->free_heap);
    cJSON_AddNumberToObject(root, "wifi_rssi", info->wifi_rssi);
    cJSON_AddStringToObject(root, "state", 
                           info->state == SYSTEM_STATE_RUNNING ? "running" : "error");
    
    char *json_string = cJSON_Print(root);
    if (json_string) {
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, TOPIC_STATUS, 
                                             json_string, 0, CONFIG_IAQ_MQTT_QOS, false);
        ESP_LOGD(TAG, "Published status, msg_id=%d", msg_id);
        free(json_string);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t mqtt_publish_sensor_data(const sensor_data_t *data)
{
    if (!s_mqtt_connected || !data) {
        return ESP_FAIL;
    }
    
    /* Create JSON with sensor data */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", data->temperature);
    cJSON_AddNumberToObject(root, "humidity", data->humidity);
    cJSON_AddNumberToObject(root, "pressure", data->pressure);
    cJSON_AddNumberToObject(root, "co2", data->co2);
    cJSON_AddNumberToObject(root, "pm1_0", data->pm1_0);
    cJSON_AddNumberToObject(root, "pm2_5", data->pm2_5);
    cJSON_AddNumberToObject(root, "pm10", data->pm10);
    cJSON_AddNumberToObject(root, "voc_index", data->voc_index);
    cJSON_AddNumberToObject(root, "nox_index", data->nox_index);
    cJSON_AddNumberToObject(root, "timestamp", data->timestamp);
    
    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string) {
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, TOPIC_STATE, 
                                             json_string, 0, CONFIG_IAQ_MQTT_QOS, false);
        ESP_LOGD(TAG, "Published sensor data, msg_id=%d", msg_id);
        free(json_string);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

bool mqtt_manager_is_connected(void)
{
    return s_mqtt_connected;
}