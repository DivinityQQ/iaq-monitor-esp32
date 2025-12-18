/* components/connectivity/mqtt_manager.c */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#ifdef CONFIG_IAQ_MQTT_TLS_TRUST_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "iaq_data.h"
#include "iaq_config.h"
#include "sensor_coordinator.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "time_sync.h"
#include "power_board.h"
#include "iaq_profiler.h"
#include "iaq_json.h"
#include "pm_guard.h"

static const char *TAG = "MQTT_MGR";

static iaq_system_context_t *s_system_ctx = NULL;
static esp_timer_handle_t s_health_timer = NULL;
static esp_timer_handle_t s_state_timer = NULL;
static esp_timer_handle_t s_metrics_timer = NULL;
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
static esp_timer_handle_t s_diagnostics_timer = NULL;
#endif
#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
static esp_timer_handle_t s_power_timer = NULL;
#endif

typedef enum {
    MQTT_PUBLISH_EVENT_HEALTH = 0,
    MQTT_PUBLISH_EVENT_STATE,
    MQTT_PUBLISH_EVENT_METRICS,
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
    MQTT_PUBLISH_EVENT_DIAGNOSTICS,
#endif
#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
    MQTT_PUBLISH_EVENT_POWER,
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
#define TOPIC_POWER     TOPIC_PREFIX "/power"
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
#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
static void mqtt_power_timer_callback(void *arg);
#endif
static void mqtt_publish_worker_task(void *arg);
static esp_err_t ensure_publish_timers_started(void);
static bool enqueue_publish_event(mqtt_publish_event_t event);
static esp_err_t start_periodic_timer(esp_timer_handle_t *handle, const esp_timer_create_args_t *args, uint64_t period_us);
static bool parse_co2_calibration_payload(const char *payload, int *ppm_out);
static esp_err_t publish_json(const char *topic, cJSON *obj);
/* Public publish functions declared in mqtt_manager.h */

/* Embedded TLS assets (conditionally defined from CMake if files exist) */
#ifdef IAQ_HAS_CA_PEM
extern const uint8_t _binary_components_connectivity_certs_ca_pem_start[];
extern const uint8_t _binary_components_connectivity_certs_ca_pem_end[];
#endif
#ifdef IAQ_HAS_CLIENT_CERT
extern const uint8_t _binary_components_connectivity_certs_client_crt_pem_start[];
extern const uint8_t _binary_components_connectivity_certs_client_crt_pem_end[];
#endif
#ifdef IAQ_HAS_CLIENT_KEY
extern const uint8_t _binary_components_connectivity_certs_client_key_pem_start[];
extern const uint8_t _binary_components_connectivity_certs_client_key_pem_end[];
#endif

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
        esp_mqtt_client_enqueue(s_mqtt_client, topic, json_string, 0, CONFIG_IAQ_MQTT_CRITICAL_QOS, 1, true);
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
    /* Only enqueue if connected - prevents queue churn and log noise when offline.
     * Worker still drains queue on disconnect as a safety net. */
    if (!s_mqtt_connected) {
        return false;
    }
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

    /* Apply TLS settings if using mqtts:// */
    const bool using_tls = (strncmp(s_broker_url, "mqtts://", 8) == 0);
    if (using_tls) {
#if CONFIG_IAQ_MQTT_TLS_TRUST_INSECURE
        ESP_LOGW(TAG, "MQTTS configured without server verification (INSECURE)");
        mqtt_cfg.broker.verification.certificate = NULL;
        mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
#elif CONFIG_IAQ_MQTT_TLS_TRUST_BUNDLE
        ESP_LOGI(TAG, "MQTTS using certificate bundle for server verification");
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#elif CONFIG_IAQ_MQTT_TLS_TRUST_CA_PEM
#ifdef IAQ_HAS_CA_PEM
        ESP_LOGI(TAG, "MQTTS using embedded Root CA PEM");
        mqtt_cfg.broker.verification.certificate = (const char *)_binary_components_connectivity_certs_ca_pem_start;
#else
        ESP_LOGW(TAG, "IAQ_MQTT_TLS_TRUST_CA_PEM enabled but no ca.pem embedded; TLS verify may fail");
#endif
#endif

#if CONFIG_IAQ_MQTT_TLS_SKIP_COMMON_NAME_CHECK
        mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
#endif

#if CONFIG_IAQ_MQTT_TLS_AWS_IOT_ALPN
        static const char *aws_alpn[] = { "x-amzn-mqtt-ca", NULL };
        mqtt_cfg.broker.verification.alpn_protos = aws_alpn;
#endif

#if CONFIG_IAQ_MQTT_MTLS_ENABLE
#if defined(IAQ_HAS_CLIENT_CERT) && defined(IAQ_HAS_CLIENT_KEY)
        ESP_LOGI(TAG, "Mutual TLS enabled (client cert + key)");
        mqtt_cfg.credentials.authentication.certificate = (const char *)_binary_components_connectivity_certs_client_crt_pem_start;
        mqtt_cfg.credentials.authentication.key = (const char *)_binary_components_connectivity_certs_client_key_pem_start;
#else
        ESP_LOGW(TAG, "IAQ_MQTT_MTLS_ENABLE set but client cert/key not embedded");
#endif
#endif
    }

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
            TASK_STACK_MQTT_MANAGER,
            NULL,
            TASK_PRIORITY_MQTT_MANAGER,
            &s_publish_task_handle,
            TASK_CORE_MQTT_MANAGER
        );
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create MQTT publish worker task");
            return ESP_FAIL;
        }
        iaq_profiler_register_task("mqtt_publish", s_publish_task_handle, TASK_STACK_MQTT_MANAGER);
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

    /* Do not start MQTT unless WiFi is connected (IP acquired). */
    if (!wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "WiFi not connected; deferring MQTT start until WiFi connects");
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
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_mqtt_client_stop returned %s; destroying client anyway", esp_err_to_name(ret));
    }
    /* Always destroy to ensure a clean slate regardless of start state */
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
    s_mqtt_connected = false;
    IAQ_DATA_WITH_LOCK() { iaq_data_get()->system.mqtt_connected = false; }
    xEventGroupClearBits(s_system_ctx->event_group, MQTT_CONNECTED_BIT);
    ESP_LOGI(TAG, "MQTT client stopped and destroyed");
    return ESP_OK;
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
    char pressure_delta_name[48];
    snprintf(pressure_delta_name, sizeof(pressure_delta_name), "Pressure Change (%d hr)", CONFIG_METRICS_PRESSURE_TREND_WINDOW_HR);
    ha_publish_sensor_config(device, "pressure_trend", "Pressure Trend", TOPIC_METRICS, NULL, NULL, "{{ value_json.pressure.trend }}", "mdi:trending-up");
    ha_publish_sensor_config(device, "pressure_delta", pressure_delta_name, TOPIC_METRICS, "pressure", "hPa", "{{ value_json.pressure.delta_hpa }}", NULL);
    ha_publish_sensor_config(device, "co2_rate", "CO2 Rate", TOPIC_METRICS, NULL, "ppm/hr", "{{ value_json.co2_rate_ppm_hr }}", "mdi:trending-up");
    ha_publish_sensor_config(device, "pm25_spike", "PM2.5 Spike Detected", TOPIC_METRICS, NULL, NULL, "{{ value_json.pm25_spike_detected }}", "mdi:alert");

    cJSON_Delete(device);
    ESP_LOGI(TAG, "Home Assistant discovery announced");
}


esp_err_t mqtt_publish_status(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = iaq_json_build_health(data);
    return publish_json(TOPIC_HEALTH, root);
}

/* Unified topics architecture: /state (fused values), /metrics (derived), /health (diagnostics). */

/* Topic publishing helpers */
static esp_err_t publish_json(const char *topic, cJSON *obj)
{
    if (!s_mqtt_connected || !obj || !topic) { if (obj) cJSON_Delete(obj); return ESP_FAIL; }
    pm_guard_lock_cpu();
    char *json_string = cJSON_PrintUnformatted(obj);
    if (json_string) {
        int msg_id = esp_mqtt_client_enqueue(s_mqtt_client, topic, json_string, 0, CONFIG_IAQ_MQTT_TELEMETRY_QOS, 0, true);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "MQTT enqueue failed (topic=%s, msg_id=%d), dropping message", topic, msg_id);
            free(json_string);
            cJSON_Delete(obj);
            pm_guard_unlock_cpu();
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Enqueued %s, msg_id=%d", topic, msg_id);
        free(json_string);
    }
    pm_guard_unlock_cpu();
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
    cJSON *root = iaq_json_build_state(data);
    return publish_json(TOPIC_STATE, root);
}

/**
 * Publish detailed /metrics topic with all derived calculations.
 * This provides full breakdown of AQI, comfort, trends, and scores.
 */
esp_err_t mqtt_publish_metrics(const iaq_data_t *data)
{
    if (!s_mqtt_connected || !data) return ESP_FAIL;
    cJSON *root = iaq_json_build_metrics(data);
    return publish_json(TOPIC_METRICS, root);
}

#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
/**
 * Publish /power topic (PowerFeather snapshot).
 */
esp_err_t mqtt_publish_power(void)
{
    if (!s_mqtt_connected) return ESP_FAIL;
    cJSON *root = iaq_json_build_power();
    return publish_json(TOPIC_POWER, root);
}
#endif

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
    if (!isnan(data->raw.temp_c)) cJSON_AddNumberToObject(raw, "temp_c", round(data->raw.temp_c * 10.0) / 10.0);
    if (!isnan(data->raw.rh_pct)) cJSON_AddNumberToObject(raw, "rh_pct", round(data->raw.rh_pct * 10.0) / 10.0);
    if (!isnan(data->raw.pressure_pa)) cJSON_AddNumberToObject(raw, "pressure_hpa", round(data->raw.pressure_pa / 10.0) / 10.0);  /* Pa -> hPa */
    if (!isnan(data->raw.pm1_ugm3)) cJSON_AddNumberToObject(raw, "pm1_ugm3", round(data->raw.pm1_ugm3 * 10.0) / 10.0);
    if (!isnan(data->raw.pm25_ugm3)) cJSON_AddNumberToObject(raw, "pm25_ugm3", round(data->raw.pm25_ugm3 * 10.0) / 10.0);
    if (!isnan(data->raw.pm10_ugm3)) cJSON_AddNumberToObject(raw, "pm10_ugm3", round(data->raw.pm10_ugm3 * 10.0) / 10.0);
    if (!isnan(data->raw.co2_ppm)) cJSON_AddNumberToObject(raw, "co2_ppm", round(data->raw.co2_ppm));
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

    /* Fusion ABC diagnostics */
    cJSON *abc = cJSON_CreateObject();
    cJSON_AddNumberToObject(abc, "baseline_ppm", data->fusion_diag.co2_abc_baseline_ppm);
    cJSON_AddNumberToObject(abc, "confidence_pct", data->fusion_diag.co2_abc_confidence_pct);
    cJSON_AddItemToObject(root, "abc", abc);

    /* Senseair S8 diagnostics (from iaq_data, not driver) */
    if (data->hw_diag.s8_diag_valid) {
        cJSON *s8j = cJSON_CreateObject();
        cJSON_AddNumberToObject(s8j, "addr", data->hw_diag.s8_addr);
        cJSON_AddNumberToObject(s8j, "serial", data->hw_diag.s8_serial);
        cJSON_AddNumberToObject(s8j, "meter_status", data->hw_diag.s8_meter_status);
        cJSON_AddBoolToObject(s8j, "abc_enabled", data->hw_diag.s8_abc_enabled);
        cJSON_AddNumberToObject(s8j, "abc_period_hours", data->hw_diag.s8_abc_period_hours);
        cJSON_AddItemToObject(root, "s8_diag", s8j);
    }

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
                int msg_id = esp_mqtt_client_subscribe(client, TOPIC_COMMAND, CONFIG_IAQ_MQTT_CRITICAL_QOS);
                ESP_LOGD(TAG, "Subscribing to %s, msg_id=%d", TOPIC_COMMAND, msg_id);
            }
            esp_mqtt_client_enqueue(client, TOPIC_STATUS, "online", 0, CONFIG_IAQ_MQTT_CRITICAL_QOS, 1, true);
            mqtt_publish_ha_discovery();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            IAQ_DATA_WITH_LOCK() { iaq_data_get()->system.mqtt_connected = false; }
            xEventGroupClearBits(s_system_ctx->event_group, MQTT_CONNECTED_BIT);

            /* Drain publish queue to prevent stale bursts after reconnect */
            if (s_publish_queue) {
                mqtt_publish_event_t discard;
                int drained = 0;
                while (xQueueReceive(s_publish_queue, &discard, 0) == pdTRUE) {
                    drained++;
                }
                if (drained > 0) {
                    ESP_LOGD(TAG, "Drained %d pending publish events on disconnect", drained);
                }
            }
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

    /* After first one-shot trigger, switch to periodic mode */
    if (s_state_timer && !esp_timer_is_active(s_state_timer)) {
        esp_timer_start_periodic(s_state_timer, CONFIG_MQTT_STATE_PUBLISH_INTERVAL_SEC * 1000000ULL);
    }
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

    /* After first one-shot trigger, switch to periodic mode */
    if (s_metrics_timer && !esp_timer_is_active(s_metrics_timer)) {
        esp_timer_start_periodic(s_metrics_timer, CONFIG_MQTT_METRICS_PUBLISH_INTERVAL_SEC * 1000000ULL);
    }
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

    /* After first one-shot trigger, switch to periodic mode */
    if (s_diagnostics_timer && !esp_timer_is_active(s_diagnostics_timer)) {
        esp_timer_start_periodic(s_diagnostics_timer, CONFIG_MQTT_DIAGNOSTICS_PUBLISH_INTERVAL_SEC * 1000000ULL);
    }
}
#endif /* CONFIG_MQTT_PUBLISH_DIAGNOSTICS */

#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
/**
 * MQTT power publishing timer callback.
 * Publishes /power topic (PowerFeather snapshot). Uses same cadence as /state.
 */
static void mqtt_power_timer_callback(void *arg)
{
    (void)arg;
    enqueue_publish_event(MQTT_PUBLISH_EVENT_POWER);

    if (s_power_timer && !esp_timer_is_active(s_power_timer)) {
        esp_timer_start_periodic(s_power_timer, CONFIG_MQTT_STATE_PUBLISH_INTERVAL_SEC * 1000000ULL);
    }
}
#endif /* CONFIG_IAQ_MQTT_PUBLISH_POWER */


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
    /* Stagger timer starts by 5 seconds each to prevent simultaneous firing
     * and flatten CPU/network bursts. Health fires immediately, state after 5s,
     * metrics after 10s, diagnostics after 15s. */

    esp_err_t ret;

    /* Health timer - starts immediately */
    const esp_timer_create_args_t health_args = {
        .callback = &mqtt_health_timer_callback,
        .name = "mqtt_health"
    };
    ret = start_periodic_timer(&s_health_timer, &health_args, STATUS_PUBLISH_INTERVAL_MS * 1000ULL);
    if (ret != ESP_OK) {
        return ret;
    }

    /* State timer - stagger by 5 seconds */
    if (s_state_timer == NULL) {
        const esp_timer_create_args_t state_args = {
            .callback = &mqtt_state_timer_callback,
            .name = "mqtt_state"
        };
        ret = esp_timer_create(&state_args, &s_state_timer);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (!esp_timer_is_active(s_state_timer)) {
        /* Start with 5 second initial delay, then periodic */
        ret = esp_timer_start_once(s_state_timer, 5000000ULL);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }

    /* Metrics timer - stagger by 10 seconds */
    if (s_metrics_timer == NULL) {
        const esp_timer_create_args_t metrics_args = {
            .callback = &mqtt_metrics_timer_callback,
            .name = "mqtt_metrics"
        };
        ret = esp_timer_create(&metrics_args, &s_metrics_timer);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (!esp_timer_is_active(s_metrics_timer)) {
        /* Start with 10 second initial delay, then periodic */
        ret = esp_timer_start_once(s_metrics_timer, 10000000ULL);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }

#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
    /* Diagnostics timer - stagger by 15 seconds */
    if (s_diagnostics_timer == NULL) {
        const esp_timer_create_args_t diag_args = {
            .callback = &mqtt_diagnostics_timer_callback,
            .name = "mqtt_diag"
        };
        ret = esp_timer_create(&diag_args, &s_diagnostics_timer);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (!esp_timer_is_active(s_diagnostics_timer)) {
        /* Start with 15 second initial delay, then periodic */
        ret = esp_timer_start_once(s_diagnostics_timer, 15000000ULL);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }
#endif
#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
    /* Power timer - share state cadence (starts after 5s) */
    if (s_power_timer == NULL) {
        const esp_timer_create_args_t power_args = {
            .callback = &mqtt_power_timer_callback,
            .name = "mqtt_power"
        };
        ret = esp_timer_create(&power_args, &s_power_timer);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (!esp_timer_is_active(s_power_timer)) {
        ret = esp_timer_start_once(s_power_timer, 5000000ULL);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }
#endif

    return ESP_OK;
}

static void mqtt_publish_worker_task(void *arg)
{
    (void)arg;
    mqtt_publish_event_t event;
    iaq_data_t snapshot;

    /* Subscribe this task to the Task Watchdog Timer for deadlock detection */
    esp_err_t wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add MQTT worker to TWDT: %s", esp_err_to_name(wdt_ret));
    }

    while (true) {
        /* Reset watchdog - confirms task is still running */
        if (wdt_ret == ESP_OK) {
            esp_task_wdt_reset();
        }

        /* Block on first event with timeout to prevent permanent blocking */
        if (xQueueReceive(s_publish_queue, &event, pdMS_TO_TICKS(5000)) != pdTRUE) {
            continue;  /* Timeout or error - loop to reset watchdog */
        }

        /* Reset watchdog after receiving event */
        if (wdt_ret == ESP_OK) {
            esp_task_wdt_reset();
        }

        if (!mqtt_manager_is_connected()) {
            continue;
        }

        /* Coalesce pending events: drain queue and OR into bitmask.
         * This reduces lock contention when multiple timers fire close together. */
        uint8_t pending_events = (1 << event);
        mqtt_publish_event_t next_event;
        while (xQueueReceive(s_publish_queue, &next_event, 0) == pdTRUE) {
            pending_events |= (1 << next_event);
        }

        /* Take single snapshot for all pending publications */
        IAQ_DATA_WITH_LOCK() {
            snapshot = *iaq_data_get();
        }

        /* Publish all requested topics from single snapshot */
        if (pending_events & (1 << MQTT_PUBLISH_EVENT_HEALTH)) {
            iaq_prof_ctx_t p = iaq_prof_start(IAQ_METRIC_MQTT_HEALTH);
            mqtt_publish_status(&snapshot);
            iaq_prof_end(p);
            if (wdt_ret == ESP_OK) esp_task_wdt_reset();
        }
        if (pending_events & (1 << MQTT_PUBLISH_EVENT_STATE)) {
            iaq_prof_ctx_t p = iaq_prof_start(IAQ_METRIC_MQTT_STATE);
            mqtt_publish_state(&snapshot);
            iaq_prof_end(p);
            if (wdt_ret == ESP_OK) esp_task_wdt_reset();
        }
        if (pending_events & (1 << MQTT_PUBLISH_EVENT_METRICS)) {
            iaq_prof_ctx_t p = iaq_prof_start(IAQ_METRIC_MQTT_METRICS);
            mqtt_publish_metrics(&snapshot);
            iaq_prof_end(p);
            if (wdt_ret == ESP_OK) esp_task_wdt_reset();
        }
#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
        if (pending_events & (1 << MQTT_PUBLISH_EVENT_DIAGNOSTICS)) {
            iaq_prof_ctx_t p = iaq_prof_start(IAQ_METRIC_MQTT_DIAG);
        mqtt_publish_diagnostics(&snapshot);
        iaq_prof_end(p);
        if (wdt_ret == ESP_OK) esp_task_wdt_reset();
    }
#endif
#ifdef CONFIG_IAQ_MQTT_PUBLISH_POWER
    if (pending_events & (1 << MQTT_PUBLISH_EVENT_POWER)) {
        mqtt_publish_power();
        if (wdt_ret == ESP_OK) esp_task_wdt_reset();
    }
#endif
    }
}
