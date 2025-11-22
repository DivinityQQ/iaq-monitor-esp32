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

    /* Initialize RAW sensor values */
    g_iaq_data.raw.temp_c = NAN;
    g_iaq_data.raw.rh_pct = NAN;
    g_iaq_data.raw.pressure_pa = NAN;
    g_iaq_data.raw.mcu_temp_c = NAN;
    g_iaq_data.raw.co2_ppm = NAN;
    g_iaq_data.raw.pm1_ugm3 = NAN;
    g_iaq_data.raw.pm25_ugm3 = NAN;
    g_iaq_data.raw.pm10_ugm3 = NAN;
    g_iaq_data.raw.voc_index = UINT16_MAX;
    g_iaq_data.raw.nox_index = UINT16_MAX;

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
    g_iaq_data.metrics.comfort_score = UINT8_MAX;  // 255 = unknown/invalid
    g_iaq_data.metrics.comfort_category = "unknown";
    g_iaq_data.metrics.co2_score = UINT8_MAX;  // 255 = unknown/invalid
    g_iaq_data.metrics.voc_category = "unknown";  // Tier 1
    g_iaq_data.metrics.nox_category = "unknown";  // Tier 1
    g_iaq_data.metrics.overall_iaq_score = UINT8_MAX;  // 255 = unknown/invalid
    g_iaq_data.metrics.mold_risk_score = UINT8_MAX;  // Tier 1, 255 = unknown/invalid
    g_iaq_data.metrics.mold_risk_category = "unknown";  // Tier 1
    g_iaq_data.metrics.pressure_trend = PRESSURE_TREND_UNKNOWN;
    g_iaq_data.metrics.pressure_delta_hpa = NAN;
    g_iaq_data.metrics.pressure_window_hours = NAN;
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

    /* Initialize HW diagnostics */
    g_iaq_data.hw_diag.s8_diag_valid = false;
    g_iaq_data.hw_diag.s8_addr = 0;
    g_iaq_data.hw_diag.s8_meter_status = 0;
    g_iaq_data.hw_diag.s8_serial = 0;
    g_iaq_data.hw_diag.s8_abc_period_hours = 0;
    g_iaq_data.hw_diag.s8_abc_enabled = false;

    /* Power snapshot */
    g_iaq_data.power.available = false;
    g_iaq_data.power.updated_us = 0;
    g_iaq_data.power.supply_mv = 0;
    g_iaq_data.power.supply_ma = 0;
    g_iaq_data.power.batt_mv = 0;
    g_iaq_data.power.batt_ma = 0;
    g_iaq_data.power.charge_pct = 0;
    g_iaq_data.power.health_pct = 0;
    g_iaq_data.power.cycles = 0;
    g_iaq_data.power.time_left_min = 0;
    g_iaq_data.power.batt_temp_c = NAN;
    g_iaq_data.power.maintain_mv = 0;
    g_iaq_data.power.en = false;
    g_iaq_data.power.v3v_on = false;
    g_iaq_data.power.vsqt_on = false;
    g_iaq_data.power.stat_on = false;
    g_iaq_data.power.charging_on = false;
    g_iaq_data.power.charge_limit_ma = 0;
    g_iaq_data.power.alarm_low_v_mv = 0;
    g_iaq_data.power.alarm_high_v_mv = 0;
    g_iaq_data.power.alarm_low_pct = 0;

    /* Initialize all validity flags to false */
    g_iaq_data.valid.temp_c = false;
    g_iaq_data.valid.mcu_temp_c = false;
    g_iaq_data.valid.rh_pct = false;
    g_iaq_data.valid.pressure_pa = false;
    g_iaq_data.valid.co2_ppm = false;
    g_iaq_data.valid.pm1_ugm3 = false;
    g_iaq_data.valid.pm25_ugm3 = false;
    g_iaq_data.valid.pm10_ugm3 = false;
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
