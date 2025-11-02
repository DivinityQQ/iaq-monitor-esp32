/* components/iaq_profiler/iaq_profiler.c */
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "iaq_profiler.h"
#include "iaq_data.h"
#include "esp_wifi.h"

static const char *TAG = "IAQ_PROF";

typedef struct {
    uint32_t count;
    uint64_t total_us;
    uint32_t max_us;
    uint32_t min_us;
    uint32_t last_us;
} iaq_metric_t;

typedef struct {
    const char *name;
    TaskHandle_t handle;
    uint32_t stack_size_bytes;
} iaq_task_entry_t;

#define IAQ_MAX_TASKS 8

static iaq_metric_t s_metrics[IAQ_METRIC_MAX];
static iaq_task_entry_t s_tasks[IAQ_MAX_TASKS];
static int s_task_count = 0;
static uint64_t s_window_start_us = 0;

/* Small critical section for metric updates */
#if CONFIG_IAQ_PROFILING
static portMUX_TYPE s_metrics_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

void iaq_profiler_init(void)
{
    static bool inited = false;
    if (inited) return;
    memset(s_metrics, 0, sizeof(s_metrics));
    memset(s_tasks, 0, sizeof(s_tasks));
    s_task_count = 0;
    s_window_start_us = esp_timer_get_time();
    inited = true;
}

void iaq_profiler_register_task(const char *name, TaskHandle_t handle, uint32_t stack_size_bytes)
{
#if CONFIG_IAQ_PROFILING && CONFIG_IAQ_PROFILING_TASK_STACKS
    if (!handle || !name) return;
    if (s_task_count >= IAQ_MAX_TASKS) return;
    s_tasks[s_task_count].name = name;
    s_tasks[s_task_count].handle = handle;
    s_tasks[s_task_count].stack_size_bytes = stack_size_bytes;
    s_task_count++;
#else
    (void)name; (void)handle; (void)stack_size_bytes;
#endif
}

void iaq_profiler_unregister_task(TaskHandle_t handle)
{
#if CONFIG_IAQ_PROFILING && CONFIG_IAQ_PROFILING_TASK_STACKS
    if (!handle) return;
    for (int i = 0; i < s_task_count; ++i) {
        if (s_tasks[i].handle == handle) {
            s_tasks[i].handle = NULL;
            s_tasks[i].name = s_tasks[i].name ? s_tasks[i].name : "(task)";
            /* keep entry to avoid shifting; will be skipped in report */
            break;
        }
    }
#else
    (void)handle;
#endif
}

void iaq_profiler_record(int metric_id, uint32_t duration_us)
{
#if CONFIG_IAQ_PROFILING
    if (metric_id < 0 || metric_id >= IAQ_METRIC_MAX) return;
    portENTER_CRITICAL(&s_metrics_lock);
    iaq_metric_t *m = &s_metrics[metric_id];
    m->count++;
    m->total_us += duration_us;
    m->last_us = duration_us;
    if (m->max_us < duration_us) m->max_us = duration_us;
    if (m->min_us == 0 || m->min_us > duration_us) m->min_us = duration_us;
    portEXIT_CRITICAL(&s_metrics_lock);
#else
    (void)metric_id; (void)duration_us;
#endif
}

/* Human-readable metric names */
#if CONFIG_IAQ_PROFILING
static const char* metric_name(int id)
{
    switch (id) {
        case IAQ_METRIC_SENSOR_MCU_READ:     return "sensor/mcu";
        case IAQ_METRIC_SENSOR_SHT45_READ:   return "sensor/sht45";
        case IAQ_METRIC_SENSOR_BMP280_READ:  return "sensor/bmp280";
        case IAQ_METRIC_SENSOR_SGP41_READ:   return "sensor/sgp41";
        case IAQ_METRIC_SENSOR_PMS5003_READ: return "sensor/pms5003";
        case IAQ_METRIC_SENSOR_PMS5003_RX:   return "sensor/pms5003_rx";
        case IAQ_METRIC_SENSOR_S8_READ:      return "sensor/s8";
        case IAQ_METRIC_FUSION_TICK:         return "fusion/tick";
        case IAQ_METRIC_METRICS_TICK:        return "metrics/tick";
        case IAQ_METRIC_MQTT_HEALTH:         return "mqtt/health";
        case IAQ_METRIC_MQTT_STATE:          return "mqtt/state";
        case IAQ_METRIC_MQTT_METRICS:        return "mqtt/metrics";
        case IAQ_METRIC_MQTT_DIAG:           return "mqtt/diag";
        case IAQ_METRIC_DISPLAY_FRAME:       return "display/frame";
        case IAQ_METRIC_WEB_STATIC:          return "web/static";
        case IAQ_METRIC_WEB_API_STATE:       return "web/api_state";
        case IAQ_METRIC_WEB_API_METRICS:     return "web/api_metrics";
        case IAQ_METRIC_WEB_API_HEALTH:      return "web/api_health";
        case IAQ_METRIC_WEB_API_WIFI_SCAN:   return "web/api_wifi_scan";
        case IAQ_METRIC_WEB_API_WIFI_POST:   return "web/api_wifi_post";
        case IAQ_METRIC_WEB_API_MQTT_POST:   return "web/api_mqtt_post";
        case IAQ_METRIC_WEB_API_SENSORS:     return "web/api_sensors";
        case IAQ_METRIC_WEB_API_SENSOR_ACTION:return "web/api_sensor_action";
        case IAQ_METRIC_WEB_WS_BROADCAST:    return "web/ws_broadcast";
        case IAQ_METRIC_WEB_WS_RX:           return "web/ws_rx";
        default: return "unknown";
    }
}
#endif

/* WiFi helpers used in both simple and comprehensive reports */
static const char* wifi_mode_to_str(wifi_mode_t m)
{
    switch (m) {
        case WIFI_MODE_STA:  return "STA";
        case WIFI_MODE_AP:   return "AP";
        case WIFI_MODE_APSTA:return "AP+STA";
        default:             return "OFF";
    }
}

static wifi_mode_t get_wifi_mode_safe(void)
{
    wifi_mode_t m = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&m) != ESP_OK) m = WIFI_MODE_NULL;
    return m;
}

#if !CONFIG_IAQ_PROFILING
static void print_simple_status(void)
{
    /* Snapshot */
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t heap_now = esp_get_free_heap_size();
    uint32_t heap_min = esp_get_minimum_free_heap_size();
    bool wifi_ok = false, mqtt_ok = false;
    int rssi = 0;
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        wifi_ok = d->system.wifi_connected;
        mqtt_ok = d->system.mqtt_connected;
        rssi = d->system.wifi_rssi;
    }

    wifi_mode_t mode = get_wifi_mode_safe();
    const char *mode_str = wifi_mode_to_str(mode);
    char wifi_str[64];
    if (mode == WIFI_MODE_AP) {
        snprintf(wifi_str, sizeof(wifi_str), "%s", mode_str);
    } else if (mode == WIFI_MODE_APSTA) {
        if (wifi_ok)
            snprintf(wifi_str, sizeof(wifi_str), "%s OK (%ddBm)", mode_str, rssi);
        else
            snprintf(wifi_str, sizeof(wifi_str), "%s Down", mode_str);
    } else if (mode == WIFI_MODE_STA) {
        if (wifi_ok)
            snprintf(wifi_str, sizeof(wifi_str), "%s OK (%ddBm)", mode_str, rssi);
        else
            snprintf(wifi_str, sizeof(wifi_str), "%s Down", mode_str);
    } else {
        snprintf(wifi_str, sizeof(wifi_str), "%s", mode_str);
    }

    unsigned h = uptime_s / 3600U;
    unsigned m = (uptime_s % 3600U) / 60U;
    unsigned s = uptime_s % 60U;
    unsigned heap_k = heap_now / 1024U;
    unsigned heap_min_k = heap_min / 1024U;

    ESP_LOGI(TAG, "Sys up %uh%um%us | heap %uk (min %uk) | WiFi %s | MQTT %s",
             h, m, s,
             heap_k, heap_min_k,
             wifi_str,
             mqtt_ok ? "OK" : "Down");
}
#endif

void iaq_status_report(void)
{
#if !CONFIG_IAQ_PROFILING
    /* Lightweight single-line status */
    print_simple_status();
    return;
#else
    /* Comprehensive, readable multi-section report */
    uint64_t now = esp_timer_get_time();
    uint32_t window_s = (uint32_t)((now - s_window_start_us) / 1000000ULL);
    if (window_s == 0) window_s = CONFIG_IAQ_PROFILING_INTERVAL_SEC;

    /* Snapshot system connectivity for a friendly header line */
    uint32_t uptime_s = (uint32_t)(now / 1000000ULL);
    bool wifi_ok = false, mqtt_ok = false; int rssi = 0;
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        wifi_ok = d->system.wifi_connected;
        mqtt_ok = d->system.mqtt_connected;
        rssi = d->system.wifi_rssi;
    }
    unsigned uh = uptime_s / 3600U, um = (uptime_s % 3600U) / 60U, us = uptime_s % 60U;
    wifi_mode_t mode = get_wifi_mode_safe();
    const char *mode_str = wifi_mode_to_str(mode);
    char wifi_str[64];
    if (mode == WIFI_MODE_AP) {
        snprintf(wifi_str, sizeof(wifi_str), "%s", mode_str);
    } else if (wifi_ok) {
        snprintf(wifi_str, sizeof(wifi_str), "%s OK (%ddBm)", mode_str, rssi);
    } else {
        snprintf(wifi_str, sizeof(wifi_str), "%s Down", mode_str);
    }
    ESP_LOGI(TAG, "[%us] Profiling Report | up %uh%um%us | WiFi %s | MQTT %s",
             (unsigned)window_s, uh, um, us, wifi_str, mqtt_ok ? "OK" : "Down");

    /* Take one consistent snapshot of metrics and reset window under lock */
    iaq_metric_t snap[IAQ_METRIC_MAX];
    portENTER_CRITICAL(&s_metrics_lock);
    memcpy(snap, s_metrics, sizeof(snap));
    memset(s_metrics, 0, sizeof(s_metrics));
    s_window_start_us = now;
    portEXIT_CRITICAL(&s_metrics_lock);

    /* Durations */
    for (int i = 0; i < IAQ_METRIC_MAX; ++i) {
        iaq_metric_t m = snap[i];
        if (m.count == 0) continue;
        uint32_t avg = (uint32_t)(m.total_us / m.count);
        ESP_LOGI(TAG, "  %-16s : n=%-4lu avg=%-6lu max=%-6lu min=%-6lu last=%-6lu",
                 metric_name(i),
                 (unsigned long)m.count,
                 (unsigned long)avg,
                 (unsigned long)m.max_us,
                 (unsigned long)m.min_us,
                 (unsigned long)m.last_us);
    }

#if CONFIG_IAQ_PROFILING_TASK_STACKS
    ESP_LOGI(TAG, "  -- Stacks (free bytes) --");
    for (int t = 0; t < s_task_count; ++t) {
        if (s_tasks[t].handle) {
            UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(s_tasks[t].handle);
            uint32_t free_bytes = (uint32_t)hwm_words * sizeof(StackType_t);
            ESP_LOGI(TAG, "  %-16s : %lu / %lu bytes",
                     s_tasks[t].name,
                     (unsigned long)free_bytes,
                     (unsigned long)s_tasks[t].stack_size_bytes);
        }
    }
#endif

    /* Heap */
    ESP_LOGI(TAG, "  -- Heap --");
    ESP_LOGI(TAG, "  now=%lu, min=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

\
#if CONFIG_IAQ_PROFILING_RUNTIME_STATS
    /* CPU usage per task since boot. Use heap buffer to avoid large stack usage. */
    ESP_LOGI(TAG, "  -- CPU (since boot) --");
    const size_t buf_size = 4096; /* generous; typical output < 2KB */
    char *buf = (char *)pvPortMalloc(buf_size);
    if (buf) {
        buf[0] = '\0';
        vTaskGetRunTimeStats(buf);
        const char *p = buf;
        while (*p) {
            const char *e = strchr(p, '\n');
            if (!e) { ESP_LOGI(TAG, "%s", p); break; }
            char line[128];
            size_t n = (size_t)(e - p);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n); line[n] = '\0';
            ESP_LOGI(TAG, "%s", line);
            p = e + 1;
        }
        vPortFree(buf);
    } else {
        ESP_LOGW(TAG, "CPU stats skipped: OOM allocating buffer");
    }
#endif

    /* Window already reset when snapshot was taken */
#endif /* CONFIG_IAQ_PROFILING */
}
