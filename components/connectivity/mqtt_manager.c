/* components/connectivity/mqtt_manager.c */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "mqtt_manager.h"
#include "iaq_data.h"
#include "iaq_config.h"

static const char *TAG = "MQTT_MGR";

static iaq_system_context_t *s_system_ctx = NULL;

/* NVS namespace for MQTT config */
#define NVS_NAMESPACE        "mqtt_config"
#define NVS_KEY_BROKER_URL   "broker_url"
#define NVS_KEY_USERNAME     "username"
#define NVS_KEY_PASSWORD     "password"

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static volatile bool s_mqtt_connected = false;
static bool s_initialized = false;
static char s_broker_url[128] = {0};
static char s_username[64] = {0};
static char s_password[64] = {0};

/* Topic definitions */
#define TOPIC_PREFIX    "iaq/" CONFIG_IAQ_DEVICE_ID
#define TOPIC_STATUS    TOPIC_PREFIX "/status"
#define TOPIC_HEALTH    TOPIC_PREFIX "/health"
#define TOPIC_COMMAND   TOPIC_PREFIX "/cmd/#"
#define TOPIC_CMD_RESTART   TOPIC_PREFIX "/cmd/restart"
#define TOPIC_CMD_CALIBRATE TOPIC_PREFIX "/cmd/calibrate"

/* Per-sensor state topics */
#define TOPIC_SENSOR_PREFIX   TOPIC_PREFIX "/sensor"
#define TOPIC_SENSOR_MCU      TOPIC_SENSOR_PREFIX "/mcu"
#define TOPIC_SENSOR_SHT41    TOPIC_SENSOR_PREFIX "/sht41"
#define TOPIC_SENSOR_BMP280   TOPIC_SENSOR_PREFIX "/bmp280"
#define TOPIC_SENSOR_SGP41    TOPIC_SENSOR_PREFIX "/sgp41"
#define TOPIC_SENSOR_PMS5003  TOPIC_SENSOR_PREFIX "/pms5003"
#define TOPIC_SENSOR_S8       TOPIC_SENSOR_PREFIX "/s8"
#define TOPIC_SENSOR_DERIVED  TOPIC_SENSOR_PREFIX "/derived"

/* Forward declarations */
static void mqtt_publish_ha_discovery(void);
static void mqtt_handle_command(const char *topic, const char *data, int data_len);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/* Helper: publish a single HA sensor discovery config */
static void ha_publish_sensor_config(cJSON *device, const char *unique_suffix, const char *name,
                                     const char *state_topic, const char *device_class,
                                     const char *unit, const char *value_template,
                                     const char *icon)
{
    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", name);
    cJSON_AddStringToObject(config, "state_topic", state_topic);
    cJSON_AddStringToObject(config, "availability_topic", TOPIC_STATUS); cJSON_AddStringToObject(config, "payload_available", "online"); cJSON_AddStringToObject(config, "payload_not_available", "offline");
    if (device_class) cJSON_AddStringToObject(config, "device_class", device_class);
    if (unit) cJSON_AddStringToObject(config, "unit_of_measurement", unit);
    if (icon) cJSON_AddStringToObject(config, "icon", icon);
    if (value_template) { cJSON_AddStringToObject(config, "value_template", value_template); }
    cJSON_AddStringToObject(config, "state_class", "measurement");
    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", CONFIG_IAQ_DEVICE_ID, unique_suffix);
    cJSON_AddStringToObject(config, "unique_id", unique_id);
    cJSON_AddItemToObject(config, "device", cJSON_Duplicate(device, true));

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", unique_id);
    char *json_string = cJSON_PrintUnformatted(config);
    if (json_string) {
        esp_mqtt_client_enqueue(s_mqtt_client, topic, json_string, 0, 1, 1, true);
        free(json_string);
    }
    cJSON_Delete(config);
}

/* Simple broker URL validator */
static bool is_valid_broker_url(const char *url)
{
    if (!url || url[0] == '\0') return false;
    const char *mqtt = "mqtt://";
    const char *mqtts = "mqtts://";
    size_t l1 = strlen(mqtt), l2 = strlen(mqtts);
    const char *host = NULL;
    if (strncmp(url, mqtt, l1) == 0) host = url + l1;
    else if (strncmp(url, mqtts, l2) == 0) host = url + l2;
    else return false;
    if (!host || host[0] == '\0') return false;
    for (const char *p = url; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') return false;
    }
    return true;
}

/* Helper: create MQTT client from current settings */
static esp_err_t create_mqtt_client(void)
{
    if (!is_valid_broker_url(s_broker_url)) return ESP_ERR_INVALID_ARG;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_broker_url,
        .credentials = {
            .client_id = CONFIG_IAQ_DEVICE_ID,
            .username = strlen(s_username) > 0 ? s_username : NULL,
            .authentication = { .password = strlen(s_password) > 0 ? s_password : NULL }
        },
        .session = {
            .last_will = { .topic = TOPIC_STATUS, .msg = "offline", .qos = 1, .retain = 1 },
            .keepalive = 60,
            .disable_clean_session = 0,
            .protocol_ver = MQTT_PROTOCOL_V_5,
        },
        .network = { .reconnect_timeout_ms = 10000, .timeout_ms = 10000 },
        .buffer = { .size = 2048, .out_size = 2048 },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return ret;
    }
    return ESP_OK;
}

/* Load MQTT configuration from NVS */
static esp_err_t load_mqtt_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved MQTT config in NVS, using defaults");
        strlcpy(s_broker_url, CONFIG_IAQ_MQTT_BROKER_URL, sizeof(s_broker_url));
        strlcpy(s_username, CONFIG_IAQ_MQTT_USERNAME, sizeof(s_username));
        strlcpy(s_password, CONFIG_IAQ_MQTT_PASSWORD, sizeof(s_password));
    } else {
        size_t len;
        len = sizeof(s_broker_url);
        ret = nvs_get_str(nvs_handle, NVS_KEY_BROKER_URL, s_broker_url, &len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read broker URL from NVS: %s", esp_err_to_name(ret));
            strlcpy(s_broker_url, CONFIG_IAQ_MQTT_BROKER_URL, sizeof(s_broker_url));
        }
        len = sizeof(s_username);
        ret = nvs_get_str(nvs_handle, NVS_KEY_USERNAME, s_username, &len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read username from NVS: %s", esp_err_to_name(ret));
            strlcpy(s_username, CONFIG_IAQ_MQTT_USERNAME, sizeof(s_username));
        }
        len = sizeof(s_password);
        ret = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_password, &len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read password from NVS: %s", esp_err_to_name(ret));
            strlcpy(s_password, CONFIG_IAQ_MQTT_PASSWORD, sizeof(s_password));
        }
        nvs_close(nvs_handle);
    }

    if (!is_valid_broker_url(s_broker_url)) {
        if (is_valid_broker_url(CONFIG_IAQ_MQTT_BROKER_URL)) {
            ESP_LOGW(TAG, "Invalid broker URL in NVS. Falling back to default: %s", CONFIG_IAQ_MQTT_BROKER_URL);
            strlcpy(s_broker_url, CONFIG_IAQ_MQTT_BROKER_URL, sizeof(s_broker_url));
        } else {
            ESP_LOGW(TAG, "Invalid broker URL (no valid default). MQTT will be disabled until configured.");
            s_broker_url[0] = '\0';
        }
    }

    ESP_LOGI(TAG, "Loaded MQTT config from NVS: Broker=%s", s_broker_url);
    return ESP_OK;
}

/* Save MQTT configuration to NVS */
static esp_err_t save_mqtt_config(const char *broker_url, const char *username, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_str(nvs_handle, NVS_KEY_BROKER_URL, broker_url);
    if (ret != ESP_OK) { nvs_close(nvs_handle); return ret; }
    ret = nvs_set_str(nvs_handle, NVS_KEY_USERNAME, username ? username : "");
    if (ret != ESP_OK) { nvs_close(nvs_handle); return ret; }
    ret = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password ? password : "");
    if (ret != ESP_OK) { nvs_close(nvs_handle); return ret; }
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Saved MQTT config to NVS");
    }
    return ret;
}

esp_err_t mqtt_manager_init(iaq_system_context_t *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid system context");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        ESP_LOGW(TAG, "MQTT manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MQTT client");

    /* Store system context */
    s_system_ctx = ctx;

    load_mqtt_config();

    if (!is_valid_broker_url(s_broker_url)) {
        ESP_LOGW(TAG, "MQTT disabled: invalid broker URL. Set with 'mqtt set <url> [user] [pass]'.");
        s_initialized = true;
        return ESP_OK;
    }

    esp_err_t ret = create_mqtt_client();
    if (ret != ESP_OK) return ret;

    s_initialized = true;
    ESP_LOGI(TAG, "MQTT client initialized successfully (MQTT 5.0)");
    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "MQTT manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mqtt_client == NULL) {
        if (!is_valid_broker_url(s_broker_url)) {
            ESP_LOGW(TAG, "MQTT not started: disabled or invalid broker. Use console to configure.");
            return ESP_OK;
        }
        esp_err_t ret = create_mqtt_client();
        if (ret != ESP_OK) return ret;
    }
    ESP_LOGI(TAG, "Starting MQTT client");
    return esp_mqtt_client_start(s_mqtt_client);
}

esp_err_t mqtt_manager_stop(void)
{
    if (!s_initialized || s_mqtt_client == NULL) return ESP_OK;
    ESP_LOGI(TAG, "Stopping MQTT client");
    esp_err_t ret = esp_mqtt_client_stop(s_mqtt_client);
    if (ret == ESP_OK) {
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        ESP_LOGI(TAG, "MQTT client stopped and destroyed");
    }
    return ret;
}

/* HA discovery */
static void mqtt_publish_ha_discovery(void)
{
    if (!s_mqtt_connected) return;
    ESP_LOGD(TAG, "Publishing Home Assistant discovery messages");

    cJSON *device = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray(); if (ids) { cJSON_AddItemToArray(ids, cJSON_CreateString(CONFIG_IAQ_DEVICE_ID)); cJSON_AddItemToObject(device, "identifiers", ids); } else { cJSON_AddStringToObject(device, "identifiers", CONFIG_IAQ_DEVICE_ID); }
    cJSON_AddStringToObject(device, "name", "IAQ Monitor");
    cJSON_AddStringToObject(device, "model", "ESP32-S3 DIY");
    cJSON_AddStringToObject(device, "manufacturer", "Homemade");
    char sw_version[32];
    snprintf(sw_version, sizeof(sw_version), "%d.%d.%d", IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);
    cJSON_AddStringToObject(device, "sw_version", sw_version);

    ha_publish_sensor_config(device, "temperature",     "Temperature",     TOPIC_SENSOR_SHT41,  "temperature",     "\xC2\xB0""C",    "{{ value_json.temperature }}",     NULL);
    ha_publish_sensor_config(device, "humidity",        "Humidity",        TOPIC_SENSOR_SHT41,  "humidity",        "%",     "{{ value_json.humidity }}",        NULL);
    ha_publish_sensor_config(device, "pressure",        "Pressure",        TOPIC_SENSOR_BMP280, "pressure",        "hPa",   "{{ value_json.pressure }}",        NULL);
    ha_publish_sensor_config(device, "co2",             "CO2",             TOPIC_SENSOR_S8,     "carbon_dioxide",  "ppm",   "{{ value_json.co2 }}",             NULL);
    ha_publish_sensor_config(device, "pm1",         "PM1.0",       TOPIC_SENSOR_PMS5003, "pm1",  "\xC2\xB5g/m\xC2\xB3", "{{ value_json.pm1_0 }}", NULL);
    ha_publish_sensor_config(device, "pm25",        "PM2.5",       TOPIC_SENSOR_PMS5003, "pm25", "\xC2\xB5g/m\xC2\xB3", "{{ value_json.pm2_5 }}", NULL);
    ha_publish_sensor_config(device, "pm10",        "PM10",        TOPIC_SENSOR_PMS5003, "pm10", "\xC2\xB5g/m\xC2\xB3", "{{ value_json.pm10 }}",  NULL);
    ha_publish_sensor_config(device, "voc",             "VOC Index",       TOPIC_SENSOR_SGP41,  NULL,               NULL,     "{{ value_json.voc_index }}",       "mdi:chemical-weapon");
    ha_publish_sensor_config(device, "nox",             "NOx Index",       TOPIC_SENSOR_SGP41,  NULL,               NULL,     "{{ value_json.nox_index }}",       "mdi:smog");
    ha_publish_sensor_config(device, "aqi",             "AQI",             TOPIC_SENSOR_DERIVED,"aqi",             NULL,     "{{ value_json.aqi }}",             NULL);
    ha_publish_sensor_config(device, "mcu_temp", "MCU Temperature", TOPIC_SENSOR_MCU,    "temperature",     "\xC2\xB0""C",    "{{ value_json.mcu_temperature }}", NULL);

    cJSON_Delete(device);
    ESP_LOGI(TAG, "Home Assistant discovery announced");
}


esp_err_t mqtt_publish_status(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime", data->system.uptime_seconds);
    cJSON_AddNumberToObject(root, "wifi_rssi", data->system.wifi_rssi);
    cJSON_AddNumberToObject(root, "free_heap", data->system.free_heap);
    cJSON *sensors_ok = cJSON_CreateArray();
    if (data->health.sht41_ok) cJSON_AddItemToArray(sensors_ok, cJSON_CreateString("sht41"));
    if (data->health.bmp280_ok) cJSON_AddItemToArray(sensors_ok, cJSON_CreateString("bmp280"));
    if (data->health.sgp41_ok) cJSON_AddItemToArray(sensors_ok, cJSON_CreateString("sgp41"));
    if (data->health.pms5003_ok) cJSON_AddItemToArray(sensors_ok, cJSON_CreateString("pms5003"));
    if (data->health.s8_ok) cJSON_AddItemToArray(sensors_ok, cJSON_CreateString("s8"));
    cJSON_AddItemToObject(root, "sensors_ok", sensors_ok);
    cJSON_AddBoolToObject(root, "warming_up", data->warming_up);
    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string) {
        esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_HEALTH, json_string, 0, CONFIG_IAQ_MQTT_QOS, 0, false);
        free(json_string);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* Aggregated publisher removed in favor of per-sensor topics. */

/* Per-sensor publishers */
static esp_err_t publish_json(const char *topic, cJSON *obj)
{
    if (!s_mqtt_connected || !obj || !topic) { if (obj) cJSON_Delete(obj); return ESP_FAIL; }
    char *json_string = cJSON_PrintUnformatted(obj);
    if (json_string) {
        int msg_id = esp_mqtt_client_enqueue(s_mqtt_client, topic, json_string, 0, CONFIG_IAQ_MQTT_QOS, 0, false);
        ESP_LOGD(TAG, "Enqueued %s, msg_id=%d", topic, msg_id);
        free(json_string);
    }
    cJSON_Delete(obj);
    return ESP_OK;
}

esp_err_t mqtt_publish_sensor_mcu(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!isnan(data->mcu_temperature)) {
        double t = round((double)data->mcu_temperature * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "mcu_temperature", t);
    } else {
        cJSON_AddNullToObject(root, "mcu_temperature");
    }
    return publish_json(TOPIC_SENSOR_MCU, root);
}

esp_err_t mqtt_publish_sensor_sht41(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!isnan(data->temperature)) {
        double t = round((double)data->temperature * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "temperature", t);
    } else {
        cJSON_AddNullToObject(root, "temperature");
    }
    if (!isnan(data->humidity)) {
        double h = round((double)data->humidity * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "humidity", h);
    } else {
        cJSON_AddNullToObject(root, "humidity");
    }
    return publish_json(TOPIC_SENSOR_SHT41, root);
}

esp_err_t mqtt_publish_sensor_bmp280(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!isnan(data->pressure)) {
        double p = round((double)data->pressure * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "pressure", p);
    } else {
        cJSON_AddNullToObject(root, "pressure");
    }
    return publish_json(TOPIC_SENSOR_BMP280, root);
}

esp_err_t mqtt_publish_sensor_sgp41(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (data->voc_index != UINT16_MAX) cJSON_AddNumberToObject(root, "voc_index", data->voc_index); else cJSON_AddNullToObject(root, "voc_index");
    if (data->nox_index != UINT16_MAX) cJSON_AddNumberToObject(root, "nox_index", data->nox_index); else cJSON_AddNullToObject(root, "nox_index");
    return publish_json(TOPIC_SENSOR_SGP41, root);
}

esp_err_t mqtt_publish_sensor_pms5003(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!isnan(data->pm1_0)) {
        double v = round((double)data->pm1_0 * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "pm1_0", v);
    } else {
        cJSON_AddNullToObject(root, "pm1_0");
    }
    if (!isnan(data->pm2_5)) {
        double v = round((double)data->pm2_5 * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "pm2_5", v);
    } else {
        cJSON_AddNullToObject(root, "pm2_5");
    }
    if (!isnan(data->pm10)) {
        double v = round((double)data->pm10 * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "pm10", v);
    } else {
        cJSON_AddNullToObject(root, "pm10");
    }
    return publish_json(TOPIC_SENSOR_PMS5003, root);
}

esp_err_t mqtt_publish_sensor_s8(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!isnan(data->co2_ppm)) {
        double c = round((double)data->co2_ppm * 100.0) / 100.0;
        cJSON_AddNumberToObject(root, "co2", c);
    } else {
        cJSON_AddNullToObject(root, "co2");
    }
    return publish_json(TOPIC_SENSOR_S8, root);
}

esp_err_t mqtt_publish_sensor_derived(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (data->aqi != UINT16_MAX) cJSON_AddNumberToObject(root, "aqi", data->aqi); else cJSON_AddNullToObject(root, "aqi");
    if (data->comfort) cJSON_AddStringToObject(root, "comfort", data->comfort); else cJSON_AddNullToObject(root, "comfort");
    return publish_json(TOPIC_SENSOR_DERIVED, root);
}

bool mqtt_manager_is_connected(void)
{
    return s_mqtt_connected;
}

esp_err_t mqtt_manager_set_broker(const char *broker_url, const char *username, const char *password)
{
    if (!broker_url) return ESP_ERR_INVALID_ARG;
    if (strlen(broker_url) == 0 || strlen(broker_url) >= sizeof(s_broker_url)) return ESP_ERR_INVALID_ARG;
    if (!is_valid_broker_url(broker_url)) {
        ESP_LOGE(TAG, "Invalid broker URL format (expected mqtt:// or mqtts://)");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setting MQTT broker: %s", broker_url);
    esp_err_t ret = save_mqtt_config(broker_url, username, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MQTT config to NVS");
        return ret;
    }
    strlcpy(s_broker_url, broker_url, sizeof(s_broker_url));
    if (username) strlcpy(s_username, username, sizeof(s_username)); else s_username[0] = '\0';
    if (password) strlcpy(s_password, password, sizeof(s_password)); else s_password[0] = '\0';
    ESP_LOGI(TAG, "MQTT broker configuration updated. Restart MQTT to apply changes.");
    return ESP_OK;
}

esp_err_t mqtt_manager_get_broker_url(char *broker_url, size_t url_len)
{
    if (!broker_url || url_len == 0) return ESP_ERR_INVALID_ARG;
    strlcpy(broker_url, s_broker_url, url_len);
    return ESP_OK;
}

bool mqtt_manager_is_configured(void)
{
    return is_valid_broker_url(s_broker_url);
}

/* Command handling */
static void mqtt_handle_command(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "Command received on topic: %s", topic);
    if (strcmp(topic, TOPIC_CMD_RESTART) == 0) {
        ESP_LOGI(TAG, "Restart command received");
        esp_restart();
    } else if (strcmp(topic, TOPIC_CMD_CALIBRATE) == 0) {
        ESP_LOGI(TAG, "Calibrate command received");
        xEventGroupSetBits(s_system_ctx->event_group, SENSORS_CALIBRATE_BIT);
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", topic);
    }
}

/* MQTT event handler */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_mqtt_connected = true;
            IAQ_DATA_WITH_LOCK() { iaq_data_get()->system.mqtt_connected = true; }
            xEventGroupSetBits(s_system_ctx->event_group, MQTT_CONNECTED_BIT);
            {
                int msg_id = esp_mqtt_client_subscribe(client, TOPIC_COMMAND, CONFIG_IAQ_MQTT_QOS);
                ESP_LOGD(TAG, "Subscribing to %s, msg_id=%d", TOPIC_COMMAND, msg_id);
            }
            esp_mqtt_client_enqueue(client, TOPIC_STATUS, "online", 0, 1, 1, true);
            mqtt_publish_ha_discovery();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            IAQ_DATA_WITH_LOCK() { iaq_data_get()->system.mqtt_connected = false; }
            xEventGroupClearBits(s_system_ctx->event_group, MQTT_CONNECTED_BIT);
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
        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT data received");
            char topic[128] = {0};
            char data_buf[256] = {0};
            if (event->topic_len < sizeof(topic)) {
                memcpy(topic, event->topic, event->topic_len);
            } else {
                ESP_LOGW(TAG, "Topic truncated: received %d bytes, buffer size %zu", event->topic_len, sizeof(topic));
            }
            if (event->data_len < sizeof(data_buf)) {
                memcpy(data_buf, event->data, event->data_len);
            } else {
                ESP_LOGW(TAG, "Data truncated: received %d bytes, buffer size %zu", event->data_len, sizeof(data_buf));
            }
            ESP_LOGI(TAG, "Topic: %s, Data: %s", topic, data_buf);
            mqtt_handle_command(topic, data_buf, event->data_len);
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls error: 0x%x", event->error_handle->esp_tls_stack_err);
            }
            break;
        default:
            ESP_LOGD(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}



