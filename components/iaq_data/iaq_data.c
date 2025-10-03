/* components/iaq_data/iaq_data.c */
#include "iaq_data.h"
#include <math.h>
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <limits.h>

static const char *TAG = "IAQ_DATA";

/* Global instance */
static iaq_data_t g_iaq_data = {0};

esp_err_t iaq_data_init(void)
{
    ESP_LOGI(TAG, "Initializing IAQ data structure");

    /* Initialize with default values - memset FIRST before creating mutex */
    memset(&g_iaq_data, 0, sizeof(iaq_data_t));

    /* Create mutex AFTER memset */
    g_iaq_data.mutex = xSemaphoreCreateMutex();
    if (g_iaq_data.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    /* Mark no-data for all numeric fields (RAW sensor values) */
    g_iaq_data.temperature = NAN;
    g_iaq_data.humidity = NAN;
    g_iaq_data.pressure = NAN;
    g_iaq_data.pressure_trend = NAN;
    g_iaq_data.co2_ppm = NAN;
    g_iaq_data.pm1_0 = NAN;
    g_iaq_data.pm2_5 = NAN;
    g_iaq_data.pm10 = NAN;
    g_iaq_data.voc_index = UINT16_MAX;
    g_iaq_data.nox_index = UINT16_MAX;
    g_iaq_data.aqi = UINT16_MAX;
    g_iaq_data.overall_quality = 0xFF;
    g_iaq_data.mcu_temperature = NAN;
    g_iaq_data.comfort = "unknown";

    /* Initialize FUSED (compensated) sensor values */
    g_iaq_data.fused.temp_c = NAN;
    g_iaq_data.fused.rh_pct = NAN;
    g_iaq_data.fused.pressure_pa = NAN;
    g_iaq_data.fused.pm1_ugm3 = NAN;
    g_iaq_data.fused.pm25_ugm3 = NAN;
    g_iaq_data.fused.pm10_ugm3 = NAN;
    g_iaq_data.fused.co2_ppm = NAN;

    /* Initialize METRICS */
    g_iaq_data.metrics.aqi_value = UINT16_MAX;
    g_iaq_data.metrics.aqi_category = "unknown";
    g_iaq_data.metrics.aqi_dominant = "none";
    g_iaq_data.metrics.aqi_pm25_subindex = NAN;
    g_iaq_data.metrics.aqi_pm10_subindex = NAN;
    g_iaq_data.metrics.dew_point_c = NAN;
    g_iaq_data.metrics.abs_humidity_gm3 = NAN;  // Tier 1
    g_iaq_data.metrics.heat_index_c = NAN;
    g_iaq_data.metrics.comfort_score = 0;
    g_iaq_data.metrics.comfort_category = "unknown";
    g_iaq_data.metrics.co2_score = 0;
    g_iaq_data.metrics.voc_category = "unknown";  // Tier 1
    g_iaq_data.metrics.nox_category = "unknown";  // Tier 1
    g_iaq_data.metrics.overall_iaq_score = 0;
    g_iaq_data.metrics.mold_risk_score = 0;  // Tier 1
    g_iaq_data.metrics.mold_risk_category = "unknown";  // Tier 1
    g_iaq_data.metrics.pressure_trend = PRESSURE_TREND_UNKNOWN;
    g_iaq_data.metrics.pressure_delta_3hr_hpa = NAN;
    g_iaq_data.metrics.co2_rate_ppm_hr = NAN;  // Tier 1
    g_iaq_data.metrics.pm25_spike_detected = false;  // Tier 1

    /* Initialize FUSION diagnostics */
    g_iaq_data.fusion_diag.pm_rh_factor = 1.0f;  // 1.0 = no correction
    g_iaq_data.fusion_diag.co2_pressure_offset_ppm = 0.0f;
    g_iaq_data.fusion_diag.temp_self_heat_offset_c = 0.0f;
    g_iaq_data.fusion_diag.co2_abc_baseline_ppm = 400;  // Typical outdoor baseline
    g_iaq_data.fusion_diag.co2_abc_confidence_pct = 0;
    g_iaq_data.fusion_diag.pm25_quality = 100;
    g_iaq_data.fusion_diag.pm1_pm25_ratio = NAN;

    /* Initialize all validity flags to false */
    g_iaq_data.valid.temperature = false;
    g_iaq_data.valid.mcu_temperature = false;
    g_iaq_data.valid.humidity = false;
    g_iaq_data.valid.pressure = false;
    g_iaq_data.valid.co2_ppm = false;
    g_iaq_data.valid.pm1_0 = false;
    g_iaq_data.valid.pm2_5 = false;
    g_iaq_data.valid.pm10 = false;
    g_iaq_data.valid.voc_index = false;
    g_iaq_data.valid.nox_index = false;

    /* Timestamps already zeroed by memset */

    /* Initialize system status with current values (prevents zeros on first MQTT publish) */
    g_iaq_data.system.uptime_seconds = 0;
    g_iaq_data.system.free_heap = esp_get_free_heap_size();
    g_iaq_data.system.min_free_heap = esp_get_minimum_free_heap_size();
    g_iaq_data.system.wifi_rssi = 0;  /* Will be updated when WiFi connects */
    g_iaq_data.system.wifi_connected = false;
    g_iaq_data.system.mqtt_connected = false;

    ESP_LOGI(TAG, "IAQ data structure initialized");
    return ESP_OK;
}

iaq_data_t* iaq_data_get(void)
{
    return &g_iaq_data;
}

bool iaq_data_lock(uint32_t timeout_ms)
{
    if (g_iaq_data.mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY) ?
                                portMAX_DELAY :
                                pdMS_TO_TICKS(timeout_ms);

    return xSemaphoreTake(g_iaq_data.mutex, timeout_ticks) == pdTRUE;
}

void iaq_data_unlock(void)
{
    if (g_iaq_data.mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return;
    }

    xSemaphoreGive(g_iaq_data.mutex);
}
