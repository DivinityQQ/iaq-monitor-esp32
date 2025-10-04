/* components/connectivity/mqtt_manager.c */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "iaq_data.h"
#include "iaq_config.h"
#include "sensor_coordinator.h"
#include "esp_timer.h"

static const char *TAG = "MQTT_MGR";

static iaq_system_context_t *s_system_ctx = NULL;
static esp_timer_handle_t s_health_timer = NULL;
static esp_timer_handle_t s_state_timer = NULL;
static esp_timer_handle_t s_metrics_timer = NULL;
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
static esp_timer_handle_t s_diagnostics_timer = NULL;
#endif

typedef enum {
    MQTT_PUBLISH_EVENT_HEALTH = 0,
    MQTT_PUBLISH_EVENT_STATE,
    MQTT_PUBLISH_EVENT_METRICS,
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
    MQTT_PUBLISH_EVENT_DIAGNOSTICS,
#endif
} mqtt_publish_event_t;

static QueueHandle_t s_publish_queue = NULL;
static TaskHandle_t s_publish_task_handle = NULL;

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
#define TOPIC_STATE     TOPIC_PREFIX "/state"
#define TOPIC_METRICS   TOPIC_PREFIX "/metrics"
#define TOPIC_DIAGNOSTICS TOPIC_PREFIX "/diagnostics"
#define TOPIC_COMMAND   TOPIC_PREFIX "/cmd/#"
#define TOPIC_CMD_RESTART   TOPIC_PREFIX "/cmd/restart"
#define TOPIC_CMD_CALIBRATE TOPIC_PREFIX "/cmd/calibrate"

/* Forward declarations */
static void mqtt_publish_ha_discovery(void);
static void mqtt_handle_command(const char *topic, const char *data, int data_len);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_health_timer_callback(void* arg);
static void mqtt_state_timer_callback(void *arg);
static void mqtt_metrics_timer_callback(void *arg);
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
static void mqtt_diagnostics_timer_callback(void *arg);
#endif
static void mqtt_publish_worker_task(void *arg);
static esp_err_t ensure_publish_timers_started(void);
static bool enqueue_publish_event(mqtt_publish_event_t event);
static esp_err_t start_periodic_timer(esp_timer_handle_t *handle, const esp_timer_create_args_t *args, uint64_t period_us);
static bool parse_co2_calibration_payload(const char *payload, int *ppm_out);
/* Public publish functions declared in mqtt_manager.h */

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
    /* Only add state_class for numeric sensors (those with device_class or unit) */
    if (device_class || unit) {
        cJSON_AddStringToObject(config, "state_class", "measurement");
    }
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

static bool enqueue_publish_event(mqtt_publish_event_t event)
{
    if (!s_publish_queue) {
        return false;
    }
    if (xQueueSend(s_publish_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Publish queue full (event=%d)", (int)event);
        return false;
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

    if (s_publish_queue == NULL) {
        s_publish_queue = xQueueCreate(12, sizeof(mqtt_publish_event_t));
        if (s_publish_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create MQTT publish queue");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_publish_task_handle == NULL) {
        BaseType_t task_ret = xTaskCreatePinnedToCore(
            mqtt_publish_worker_task,
            "mqtt_publish",
            TASK_STACK_NETWORK_MANAGER,
            NULL,
            TASK_PRIORITY_NETWORK_MANAGER,
            &s_publish_task_handle,
            TASK_CORE_NETWORK_MANAGER
        );
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create MQTT publish worker task");
            return ESP_FAIL;
        }
    }

    esp_err_t timer_ret = ensure_publish_timers_started();
    if (timer_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT timers: %s", esp_err_to_name(timer_ret));
        return timer_ret;
    }

    esp_err_t client_ret = ESP_OK;
    if (is_valid_broker_url(s_broker_url)) {
        client_ret = create_mqtt_client();
        if (client_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create MQTT client: %s", esp_err_to_name(client_ret));
        }
    } else {
        ESP_LOGW(TAG, "MQTT disabled: invalid broker URL. Set with 'mqtt set <url> [user] [pass]'.");
    }

    s_initialized = true;

    if (client_ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client initialized successfully (MQTT 5.0)");
    }

    return client_ret;
}

esp_err_t mqtt_manager_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "MQTT manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t timer_ret = ensure_publish_timers_started();
    if (timer_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to ensure MQTT timers running: %s", esp_err_to_name(timer_ret));
        return timer_ret;
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

    /* Compensated sensor values from /state topic */
    ha_publish_sensor_config(device, "temperature",     "Temperature",     TOPIC_STATE,  "temperature",     "\xC2\xB0""C",    "{{ value_json.temp_c }}",     NULL);
    ha_publish_sensor_config(device, "humidity",        "Humidity",        TOPIC_STATE,  "humidity",        "%",     "{{ value_json.rh_pct }}",        NULL);
    ha_publish_sensor_config(device, "pressure",        "Pressure",        TOPIC_STATE, "pressure",        "hPa",   "{{ value_json.pressure_hpa }}",        NULL);
    ha_publish_sensor_config(device, "co2",             "CO2",             TOPIC_STATE,     "carbon_dioxide",  "ppm",   "{{ value_json.co2_ppm }}",             NULL);
#ifdef CONFIG_MQTT_PUBLISH_PM1
    ha_publish_sensor_config(device, "pm1",         "PM1.0",       TOPIC_STATE, "pm1",  "\xC2\xB5g/m\xC2\xB3", "{{ value_json.pm1_ugm3 }}", NULL);
#endif
    ha_publish_sensor_config(device, "pm25",        "PM2.5",       TOPIC_STATE, "pm25", "\xC2\xB5g/m\xC2\xB3", "{{ value_json.pm25_ugm3 }}", NULL);
    ha_publish_sensor_config(device, "pm10",        "PM10",        TOPIC_STATE, "pm10", "\xC2\xB5g/m\xC2\xB3", "{{ value_json.pm10_ugm3 }}",  NULL);
    ha_publish_sensor_config(device, "voc",             "VOC Index",       TOPIC_STATE,  NULL,               "index",     "{{ value_json.voc_index }}",       "mdi:chemical-weapon");
    ha_publish_sensor_config(device, "nox",             "NOx Index",       TOPIC_STATE,  NULL,               "index",     "{{ value_json.nox_index }}",       "mdi:smog");
    ha_publish_sensor_config(device, "mcu_temp", "MCU Temperature", TOPIC_STATE,    "temperature",     "\xC2\xB0""C",    "{{ value_json.mcu_temp_c }}", NULL);

    /* Basic metrics from /state topic */
    ha_publish_sensor_config(device, "aqi",             "AQI",             TOPIC_STATE,"aqi",             NULL,     "{{ value_json.aqi }}",             NULL);
    ha_publish_sensor_config(device, "comfort_score", "Comfort Score", TOPIC_STATE, NULL, "score", "{{ value_json.comfort_score }}", "mdi:thermometer-lines");

    /* Detailed metrics from /metrics topic */
    ha_publish_sensor_config(device, "aqi_category", "AQI Category", TOPIC_METRICS, NULL, NULL, "{{ value_json.aqi.category }}", "mdi:air-filter");
    ha_publish_sensor_config(device, "aqi_dominant", "AQI Dominant Pollutant", TOPIC_METRICS, NULL, NULL, "{{ value_json.aqi.dominant }}", "mdi:molecule");
    ha_publish_sensor_config(device, "dew_point", "Dew Point", TOPIC_METRICS, "temperature", "\xC2\xB0""C", "{{ value_json.comfort.dew_point_c }}", NULL);
    ha_publish_sensor_config(device, "abs_humidity", "Absolute Humidity", TOPIC_METRICS, "absolute_humidity", "g/m\xC2\xB3", "{{ value_json.comfort.abs_humidity_gm3 }}", NULL);
    ha_publish_sensor_config(device, "heat_index", "Heat Index", TOPIC_METRICS, "temperature", "\xC2\xB0""C", "{{ value_json.comfort.heat_index_c }}", "mdi:thermometer-alert");
    ha_publish_sensor_config(device, "comfort_category", "Comfort Category", TOPIC_METRICS, NULL, NULL, "{{ value_json.comfort.category }}", "mdi:sofa");
    ha_publish_sensor_config(device, "co2_score", "CO2 Score", TOPIC_METRICS, NULL, "score", "{{ value_json.co2_score }}", "mdi:air-purifier");
    ha_publish_sensor_config(device, "voc_category", "VOC Category", TOPIC_METRICS, NULL, NULL, "{{ value_json.voc_category }}", "mdi:chemical-weapon");
    ha_publish_sensor_config(device, "nox_category", "NOx Category", TOPIC_METRICS, NULL, NULL, "{{ value_json.nox_category }}", "mdi:smog");
    ha_publish_sensor_config(device, "overall_iaq_score", "Overall IAQ Score", TOPIC_METRICS, NULL, "score", "{{ value_json.overall_iaq_score }}", "mdi:air-filter");
    ha_publish_sensor_config(device, "mold_risk", "Mold Risk Score", TOPIC_METRICS, NULL, "score", "{{ value_json.mold_risk.score }}", "mdi:water-percent");
    ha_publish_sensor_config(device, "mold_category", "Mold Risk Category", TOPIC_METRICS, NULL, NULL, "{{ value_json.mold_risk.category }}", "mdi:water-alert");
    ha_publish_sensor_config(device, "pressure_trend", "Pressure Trend", TOPIC_METRICS, NULL, NULL, "{{ value_json.pressure.trend }}", "mdi:trending-up");
    ha_publish_sensor_config(device, "pressure_delta_3hr", "Pressure Change (3hr)", TOPIC_METRICS, "pressure", "hPa", "{{ value_json.pressure.delta_3hr_hpa }}", NULL);
    ha_publish_sensor_config(device, "co2_rate", "CO2 Rate", TOPIC_METRICS, NULL, "ppm/hr", "{{ value_json.co2_rate_ppm_hr }}", "mdi:trending-up");
    ha_publish_sensor_config(device, "pm25_spike", "PM2.5 Spike Detected", TOPIC_METRICS, NULL, NULL, "{{ value_json.pm25_spike_detected }}", "mdi:alert");

    cJSON_Delete(device);
    ESP_LOGI(TAG, "Home Assistant discovery announced");
}


esp_err_t mqtt_publish_status(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();

    /* System metrics */
    cJSON_AddNumberToObject(root, "uptime", data->system.uptime_seconds);
    cJSON_AddNumberToObject(root, "wifi_rssi", data->system.wifi_rssi);
    cJSON_AddNumberToObject(root, "free_heap", data->system.free_heap);

    /* Per-sensor state details (query coordinator API) */
    cJSON *sensors = cJSON_CreateObject();

    for (int i = 0; i < SENSOR_ID_MAX; i++) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info((sensor_id_t)i, &info) != ESP_OK) {
            continue;
        }

        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddStringToObject(sensor, "state", sensor_coordinator_state_to_string(info.state));
        cJSON_AddNumberToObject(sensor, "errors", info.error_count);

        int64_t now_us = esp_timer_get_time();

        if (info.last_read_us > 0) {
            int64_t age_s = (now_us - info.last_read_us) / 1000000LL;
            cJSON_AddNumberToObject(sensor, "last_read_s", (double)age_s);
        }

        if (info.state == SENSOR_STATE_WARMING) {
            int64_t remaining_us = info.warmup_deadline_us - now_us;
            if (remaining_us > 0) {
                cJSON_AddNumberToObject(sensor, "warmup_remaining_s", remaining_us / 1e6);
            }
        }

        cJSON_AddItemToObject(sensors, sensor_coordinator_id_to_name((sensor_id_t)i), sensor);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    /* Publish */
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

/**
 * Publish unified /state topic with compensated (fused) sensor values.
 * This is the primary telemetry topic - publishes what users should see.
 */
esp_err_t mqtt_publish_state(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;

    cJSON *root = cJSON_CreateObject();

    /* Fused (compensated) sensor values */
    if (!isnan(data->fused.temp_c)) {
        cJSON_AddNumberToObject(root, "temp_c", round(data->fused.temp_c * 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "temp_c");
    }

    if (!isnan(data->fused.rh_pct)) {
        cJSON_AddNumberToObject(root, "rh_pct", round(data->fused.rh_pct * 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "rh_pct");
    }

    if (!isnan(data->fused.pressure_pa)) {
        cJSON_AddNumberToObject(root, "pressure_hpa", round(data->fused.pressure_pa / 10.0) / 10.0);  /* Pa -> hPa */
    } else {
        cJSON_AddNullToObject(root, "pressure_hpa");
    }

    if (!isnan(data->fused.pm25_ugm3)) {
        cJSON_AddNumberToObject(root, "pm25_ugm3", round(data->fused.pm25_ugm3 * 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "pm25_ugm3");
    }

    if (!isnan(data->fused.pm10_ugm3)) {
        cJSON_AddNumberToObject(root, "pm10_ugm3", round(data->fused.pm10_ugm3 * 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "pm10_ugm3");
    }

#ifdef CONFIG_MQTT_PUBLISH_PM1
    if (!isnan(data->fused.pm1_ugm3)) {
        cJSON_AddNumberToObject(root, "pm1_ugm3", round(data->fused.pm1_ugm3 * 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "pm1_ugm3");
    }
#endif

    if (!isnan(data->fused.co2_ppm)) {
        cJSON_AddNumberToObject(root, "co2_ppm", round(data->fused.co2_ppm));
    } else {
        cJSON_AddNullToObject(root, "co2_ppm");
    }

    /* VOC/NOx indices (not compensated, use raw) */
    if (data->voc_index != UINT16_MAX) {
        cJSON_AddNumberToObject(root, "voc_index", data->voc_index);
    } else {
        cJSON_AddNullToObject(root, "voc_index");
    }

    if (data->nox_index != UINT16_MAX) {
        cJSON_AddNumberToObject(root, "nox_index", data->nox_index);
    } else {
        cJSON_AddNullToObject(root, "nox_index");
    }

    /* MCU temperature (not compensated) */
    if (!isnan(data->mcu_temperature)) {
        cJSON_AddNumberToObject(root, "mcu_temp_c", round(data->mcu_temperature * 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "mcu_temp_c");
    }

    /* Basic metrics (for quick overview) */
    if (data->metrics.aqi_value != UINT16_MAX) {
        cJSON_AddNumberToObject(root, "aqi", data->metrics.aqi_value);
    } else {
        cJSON_AddNullToObject(root, "aqi");
    }

    if (data->metrics.comfort_score > 0) {
        cJSON_AddNumberToObject(root, "comfort_score", data->metrics.comfort_score);
    } else {
        cJSON_AddNullToObject(root, "comfort_score");
    }

    return publish_json(TOPIC_STATE, root);
}

/**
 * Publish detailed /metrics topic with all derived calculations.
 * This provides full breakdown of AQI, comfort, trends, and scores.
 */
esp_err_t mqtt_publish_metrics(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;

    cJSON *root = cJSON_CreateObject();

    /* AQI breakdown */
    cJSON *aqi = cJSON_CreateObject();
    if (data->metrics.aqi_value != UINT16_MAX) {
        cJSON_AddNumberToObject(aqi, "value", data->metrics.aqi_value);
        cJSON_AddStringToObject(aqi, "category", data->metrics.aqi_category);
        cJSON_AddStringToObject(aqi, "dominant", data->metrics.aqi_dominant);
        if (!isnan(data->metrics.aqi_pm25_subindex)) {
            cJSON_AddNumberToObject(aqi, "pm25_subindex", round(data->metrics.aqi_pm25_subindex * 10.0) / 10.0);
        }
        if (!isnan(data->metrics.aqi_pm10_subindex)) {
            cJSON_AddNumberToObject(aqi, "pm10_subindex", round(data->metrics.aqi_pm10_subindex * 10.0) / 10.0);
        }
    }
    cJSON_AddItemToObject(root, "aqi", aqi);

    /* Comfort breakdown */
    cJSON *comfort = cJSON_CreateObject();
    cJSON_AddNumberToObject(comfort, "score", data->metrics.comfort_score);
    cJSON_AddStringToObject(comfort, "category", data->metrics.comfort_category);
    if (!isnan(data->metrics.dew_point_c)) {
        cJSON_AddNumberToObject(comfort, "dew_point_c", round(data->metrics.dew_point_c * 10.0) / 10.0);
    }
    if (!isnan(data->metrics.abs_humidity_gm3)) {
        cJSON_AddNumberToObject(comfort, "abs_humidity_gm3", round(data->metrics.abs_humidity_gm3 * 10.0) / 10.0);
    }
    if (!isnan(data->metrics.heat_index_c)) {
        cJSON_AddNumberToObject(comfort, "heat_index_c", round(data->metrics.heat_index_c * 10.0) / 10.0);
    }
    cJSON_AddItemToObject(root, "comfort", comfort);

    /* Pressure trend */
    cJSON *pressure = cJSON_CreateObject();
    const char *trend_str = "unknown";
    switch (data->metrics.pressure_trend) {
        case PRESSURE_TREND_RISING:  trend_str = "rising"; break;
        case PRESSURE_TREND_STABLE:  trend_str = "stable"; break;
        case PRESSURE_TREND_FALLING: trend_str = "falling"; break;
        default: break;
    }
    cJSON_AddStringToObject(pressure, "trend", trend_str);
    if (!isnan(data->metrics.pressure_delta_3hr_hpa)) {
        cJSON_AddNumberToObject(pressure, "delta_3hr_hpa", round(data->metrics.pressure_delta_3hr_hpa * 100.0) / 100.0);
    }
    cJSON_AddItemToObject(root, "pressure", pressure);

    /* Air quality scores */
    cJSON_AddNumberToObject(root, "co2_score", data->metrics.co2_score);
    cJSON_AddStringToObject(root, "voc_category", data->metrics.voc_category);
    cJSON_AddStringToObject(root, "nox_category", data->metrics.nox_category);
    cJSON_AddNumberToObject(root, "overall_iaq_score", data->metrics.overall_iaq_score);

    /* Mold risk */
    cJSON *mold = cJSON_CreateObject();
    cJSON_AddNumberToObject(mold, "score", data->metrics.mold_risk_score);
    cJSON_AddStringToObject(mold, "category", data->metrics.mold_risk_category);
    cJSON_AddItemToObject(root, "mold_risk", mold);

    /* Trends */
    if (!isnan(data->metrics.co2_rate_ppm_hr)) {
        cJSON_AddNumberToObject(root, "co2_rate_ppm_hr", round(data->metrics.co2_rate_ppm_hr * 10.0) / 10.0);
    }
    cJSON_AddBoolToObject(root, "pm25_spike_detected", data->metrics.pm25_spike_detected);

    return publish_json(TOPIC_METRICS, root);
}

#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
/**
 * Publish optional /diagnostics topic with raw values and fusion debug info.
 * Only compiled if CONFIG_MQTT_PUBLISH_DIAGNOSTICS is enabled.
 */
esp_err_t mqtt_publish_diagnostics(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;

    cJSON *root = cJSON_CreateObject();

    /* Raw (uncompensated) sensor values */
    cJSON *raw = cJSON_CreateObject();
    if (!isnan(data->temperature)) cJSON_AddNumberToObject(raw, "temp_c", round(data->temperature * 10.0) / 10.0);
    if (!isnan(data->humidity)) cJSON_AddNumberToObject(raw, "rh_pct", round(data->humidity * 10.0) / 10.0);
    if (!isnan(data->pressure)) cJSON_AddNumberToObject(raw, "pressure_hpa", round(data->pressure * 10.0) / 10.0);
    if (!isnan(data->pm1_0)) cJSON_AddNumberToObject(raw, "pm1_ugm3", round(data->pm1_0 * 10.0) / 10.0);
    if (!isnan(data->pm2_5)) cJSON_AddNumberToObject(raw, "pm25_ugm3", round(data->pm2_5 * 10.0) / 10.0);
    if (!isnan(data->pm10)) cJSON_AddNumberToObject(raw, "pm10_ugm3", round(data->pm10 * 10.0) / 10.0);
    if (!isnan(data->co2_ppm)) cJSON_AddNumberToObject(raw, "co2_ppm", round(data->co2_ppm));
    cJSON_AddItemToObject(root, "raw", raw);

    /* Fusion diagnostics */
    cJSON *fusion = cJSON_CreateObject();
    cJSON_AddNumberToObject(fusion, "pm_rh_factor", round(data->fusion_diag.pm_rh_factor * 1000.0) / 1000.0);
    cJSON_AddNumberToObject(fusion, "co2_pressure_offset_ppm", round(data->fusion_diag.co2_pressure_offset_ppm * 10.0) / 10.0);
    cJSON_AddNumberToObject(fusion, "temp_self_heat_offset_c", round(data->fusion_diag.temp_self_heat_offset_c * 100.0) / 100.0);
    cJSON_AddNumberToObject(fusion, "pm25_quality", data->fusion_diag.pm25_quality);
    if (!isnan(data->fusion_diag.pm1_pm25_ratio)) {
        cJSON_AddNumberToObject(fusion, "pm1_pm25_ratio", round(data->fusion_diag.pm1_pm25_ratio * 100.0) / 100.0);
    }
    cJSON_AddItemToObject(root, "fusion", fusion);

    /* ABC diagnostics */
    cJSON *abc = cJSON_CreateObject();
    cJSON_AddNumberToObject(abc, "baseline_ppm", data->fusion_diag.co2_abc_baseline_ppm);
    cJSON_AddNumberToObject(abc, "confidence_pct", data->fusion_diag.co2_abc_confidence_pct);
    cJSON_AddItemToObject(root, "abc", abc);

    return publish_json(TOPIC_DIAGNOSTICS, root);
}
#endif /* CONFIG_MQTT_PUBLISH_DIAGNOSTICS */

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

        int ppm = 400;
        bool has_payload = data && data[0] != '\0';
        if (has_payload) {
            if (!parse_co2_calibration_payload(data, &ppm)) {
                ESP_LOGW(TAG, "Invalid calibration payload: %s", data);
                return;
            }
        } else {
            ESP_LOGI(TAG, "Calibration payload empty, defaulting to 400 ppm");
        }

        esp_err_t op_res = sensor_coordinator_calibrate(SENSOR_ID_S8, ppm);
        if (op_res == ESP_OK) {
            ESP_LOGI(TAG, "CO2 calibration enqueued (%d ppm)", ppm);
        } else {
            ESP_LOGE(TAG, "Failed to queue CO2 calibration: %s", esp_err_to_name(op_res));
        }
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

/**
 * MQTT health timer callback - publishes /health topic to MQTT.
 * Runs every 30 seconds when MQTT is connected.
 * System metrics are already updated by main.c system_status_timer.
 */
static void mqtt_health_timer_callback(void* arg)
{
    (void)arg;
    enqueue_publish_event(MQTT_PUBLISH_EVENT_HEALTH);
}

/**
 * MQTT state publishing timer callback.
 * Publishes unified /state topic with compensated (fused) sensor values.
 * Runs at configurable interval (default 30s).
 */
static void mqtt_state_timer_callback(void *arg)
{
    (void)arg;
    enqueue_publish_event(MQTT_PUBLISH_EVENT_STATE);
}

/**
 * MQTT metrics publishing timer callback.
 * Publishes detailed /metrics topic with derived calculations (AQI, comfort, trends).
 * Runs at configurable interval (default 30s).
 */
static void mqtt_metrics_timer_callback(void *arg)
{
    (void)arg;
    enqueue_publish_event(MQTT_PUBLISH_EVENT_METRICS);
}

#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
/**
 * MQTT diagnostics publishing timer callback.
 * Publishes optional /diagnostics topic with raw values and fusion debug info.
 * Runs at configurable interval (default 5 minutes).
 */
static void mqtt_diagnostics_timer_callback(void *arg)
{
    (void)arg;
    enqueue_publish_event(MQTT_PUBLISH_EVENT_DIAGNOSTICS);
}
#endif /* CONFIG_MQTT_PUBLISH_DIAGNOSTICS */





static bool parse_co2_calibration_payload(const char *payload, int *ppm_out)
{
    if (!payload || !ppm_out) {
        return false;
    }

    while (*payload && isspace((unsigned char)*payload)) {
        payload++;
    }

    if (*payload == '{') {
        cJSON *root = cJSON_Parse(payload);
        if (!root) {
            return false;
        }

        const cJSON *ppm = cJSON_GetObjectItemCaseSensitive(root, "ppm");
        bool ok = false;
        if (cJSON_IsNumber(ppm)) {
            int value = (int)ppm->valuedouble;
            if (value > 0 && value <= 5000) {
                *ppm_out = value;
                ok = true;
            }
        }

        cJSON_Delete(root);
        return ok;
    }

    char *endptr = NULL;
    long value = strtol(payload, &endptr, 10);
    if (endptr == payload) {
        return false;
    }
    while (endptr && *endptr && isspace((unsigned char)*endptr)) {
        endptr++;
    }
    if (endptr && *endptr != '\0') {
        return false;
    }
    if (value <= 0 || value > 5000) {
        return false;
    }

    *ppm_out = (int)value;
    return true;
}

static esp_err_t start_periodic_timer(esp_timer_handle_t *handle,
                                      const esp_timer_create_args_t *args,
                                      uint64_t period_us)
{
    if (*handle == NULL) {
        esp_err_t ret = esp_timer_create(args, handle);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (!esp_timer_is_active(*handle)) {
        esp_err_t ret = esp_timer_start_periodic(*handle, period_us);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t ensure_publish_timers_started(void)
{
    const esp_timer_create_args_t health_args = {
        .callback = &mqtt_health_timer_callback,
        .name = "mqtt_health"
    };
    esp_err_t ret = start_periodic_timer(&s_health_timer, &health_args, STATUS_PUBLISH_INTERVAL_MS * 1000ULL);
    if (ret != ESP_OK) {
        return ret;
    }

    const esp_timer_create_args_t state_args = {
        .callback = &mqtt_state_timer_callback,
        .name = "mqtt_state"
    };
    ret = start_periodic_timer(&s_state_timer, &state_args, CONFIG_MQTT_STATE_PUBLISH_INTERVAL_SEC * 1000000ULL);
    if (ret != ESP_OK) {
        return ret;
    }

    const esp_timer_create_args_t metrics_args = {
        .callback = &mqtt_metrics_timer_callback,
        .name = "mqtt_metrics"
    };
    ret = start_periodic_timer(&s_metrics_timer, &metrics_args, CONFIG_MQTT_METRICS_PUBLISH_INTERVAL_SEC * 1000000ULL);
    if (ret != ESP_OK) {
        return ret;
    }

#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
    const esp_timer_create_args_t diag_args = {
        .callback = &mqtt_diagnostics_timer_callback,
        .name = "mqtt_diag"
    };
    ret = start_periodic_timer(&s_diagnostics_timer, &diag_args, CONFIG_MQTT_DIAGNOSTICS_PUBLISH_INTERVAL_SEC * 1000000ULL);
    if (ret != ESP_OK) {
        return ret;
    }
#endif

    return ESP_OK;
}

static void mqtt_publish_worker_task(void *arg)
{
    (void)arg;
    mqtt_publish_event_t event;
    iaq_data_t snapshot;

    while (true) {
        if (xQueueReceive(s_publish_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!mqtt_manager_is_connected()) {
            continue;
        }

        IAQ_DATA_WITH_LOCK() {
            snapshot = *iaq_data_get();
        }

        switch (event) {
            case MQTT_PUBLISH_EVENT_HEALTH:
                mqtt_publish_status(&snapshot);
                break;
            case MQTT_PUBLISH_EVENT_STATE:
                mqtt_publish_state(&snapshot);
                break;
            case MQTT_PUBLISH_EVENT_METRICS:
                mqtt_publish_metrics(&snapshot);
                break;
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
            case MQTT_PUBLISH_EVENT_DIAGNOSTICS:
                mqtt_publish_diagnostics(&snapshot);
                break;
#endif
            default:
                break;
        }
    }
}
