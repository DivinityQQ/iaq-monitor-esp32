/* components/iaq_json/iaq_json.c */
#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "cJSON.h"
#include "iaq_json.h"
#include "sensor_coordinator.h"
#include "time_sync.h"
#include <time.h>

static double round_to_1dp(double v) { return round(v * 10.0) / 10.0; }
static double round_to_2dp(double v) { return round(v * 100.0) / 100.0; }

cJSON* iaq_json_build_state(const iaq_data_t *data)
{
    if (!data) return NULL;

    cJSON *root = cJSON_CreateObject();

    /* Fused (compensated) sensor values - use validity flags instead of NaN checks */
    if (data->valid.temp_c && !isnan(data->fused.temp_c)) cJSON_AddNumberToObject(root, "temp_c", round_to_1dp(data->fused.temp_c));
    else cJSON_AddNullToObject(root, "temp_c");

    if (data->valid.rh_pct && !isnan(data->fused.rh_pct)) cJSON_AddNumberToObject(root, "rh_pct", round_to_1dp(data->fused.rh_pct));
    else cJSON_AddNullToObject(root, "rh_pct");

    /* Convert Pa -> hPa (1 hPa = 100 Pa) */
    if (data->valid.pressure_pa && !isnan(data->fused.pressure_pa)) cJSON_AddNumberToObject(root, "pressure_hpa", round_to_1dp(data->fused.pressure_pa / 100.0));
    else cJSON_AddNullToObject(root, "pressure_hpa");

    if (data->valid.pm25_ugm3 && !isnan(data->fused.pm25_ugm3)) cJSON_AddNumberToObject(root, "pm25_ugm3", round_to_1dp(data->fused.pm25_ugm3));
    else cJSON_AddNullToObject(root, "pm25_ugm3");

    if (data->valid.pm10_ugm3 && !isnan(data->fused.pm10_ugm3)) cJSON_AddNumberToObject(root, "pm10_ugm3", round_to_1dp(data->fused.pm10_ugm3));
    else cJSON_AddNullToObject(root, "pm10_ugm3");

#ifdef CONFIG_MQTT_PUBLISH_PM1
    if (data->valid.pm1_ugm3 && !isnan(data->fused.pm1_ugm3)) cJSON_AddNumberToObject(root, "pm1_ugm3", round_to_1dp(data->fused.pm1_ugm3));
    else cJSON_AddNullToObject(root, "pm1_ugm3");
#endif

    if (data->valid.co2_ppm && !isnan(data->fused.co2_ppm)) cJSON_AddNumberToObject(root, "co2_ppm", round(data->fused.co2_ppm));
    else cJSON_AddNullToObject(root, "co2_ppm");

    /* VOC/NOx indices (raw) */
    if (data->valid.voc_index && data->raw.voc_index != UINT16_MAX) cJSON_AddNumberToObject(root, "voc_index", data->raw.voc_index);
    else cJSON_AddNullToObject(root, "voc_index");

    if (data->valid.nox_index && data->raw.nox_index != UINT16_MAX) cJSON_AddNumberToObject(root, "nox_index", data->raw.nox_index);
    else cJSON_AddNullToObject(root, "nox_index");

    /* MCU temperature */
    if (data->valid.mcu_temp_c && !isnan(data->raw.mcu_temp_c)) cJSON_AddNumberToObject(root, "mcu_temp_c", round_to_1dp(data->raw.mcu_temp_c));
    else cJSON_AddNullToObject(root, "mcu_temp_c");

    /* Basic metrics */
    if (data->metrics.aqi_value != UINT16_MAX) cJSON_AddNumberToObject(root, "aqi", data->metrics.aqi_value);
    else cJSON_AddNullToObject(root, "aqi");

    /* Emit comfort_score when valid (UINT8_MAX denotes unknown) */
    if (data->metrics.comfort_score != UINT8_MAX) cJSON_AddNumberToObject(root, "comfort_score", data->metrics.comfort_score);
    else cJSON_AddNullToObject(root, "comfort_score");

    return root;
}

cJSON* iaq_json_build_metrics(const iaq_data_t *data)
{
    if (!data) return NULL;
    cJSON *root = cJSON_CreateObject();

    /* AQI breakdown */
    cJSON *aqi = cJSON_CreateObject();
    if (data->metrics.aqi_value != UINT16_MAX) cJSON_AddNumberToObject(aqi, "value", data->metrics.aqi_value);
    else cJSON_AddNullToObject(aqi, "value");
    cJSON_AddStringToObject(aqi, "category", data->metrics.aqi_category);
    cJSON_AddStringToObject(aqi, "dominant", data->metrics.aqi_dominant);
    if (!isnan(data->metrics.aqi_pm25_subindex)) cJSON_AddNumberToObject(aqi, "pm25_subindex", round_to_1dp(data->metrics.aqi_pm25_subindex));
    else cJSON_AddNullToObject(aqi, "pm25_subindex");
    if (!isnan(data->metrics.aqi_pm10_subindex)) cJSON_AddNumberToObject(aqi, "pm10_subindex", round_to_1dp(data->metrics.aqi_pm10_subindex));
    else cJSON_AddNullToObject(aqi, "pm10_subindex");
    cJSON_AddItemToObject(root, "aqi", aqi);

    /* Comfort */
    cJSON *comfort = cJSON_CreateObject();
    if (data->metrics.comfort_score != UINT8_MAX) cJSON_AddNumberToObject(comfort, "score", data->metrics.comfort_score);
    else cJSON_AddNullToObject(comfort, "score");
    cJSON_AddStringToObject(comfort, "category", data->metrics.comfort_category);
    if (!isnan(data->metrics.dew_point_c)) cJSON_AddNumberToObject(comfort, "dew_point_c", round_to_1dp(data->metrics.dew_point_c));
    else cJSON_AddNullToObject(comfort, "dew_point_c");
    if (!isnan(data->metrics.abs_humidity_gm3)) cJSON_AddNumberToObject(comfort, "abs_humidity_gm3", round_to_1dp(data->metrics.abs_humidity_gm3));
    else cJSON_AddNullToObject(comfort, "abs_humidity_gm3");
    if (!isnan(data->metrics.heat_index_c)) cJSON_AddNumberToObject(comfort, "heat_index_c", round_to_1dp(data->metrics.heat_index_c));
    else cJSON_AddNullToObject(comfort, "heat_index_c");
    cJSON_AddItemToObject(root, "comfort", comfort);

    /* Pressure */
    cJSON *pressure = cJSON_CreateObject();
    const char *trend_str = "unknown";
    switch (data->metrics.pressure_trend) {
        case PRESSURE_TREND_RISING:  trend_str = "rising"; break;
        case PRESSURE_TREND_STABLE:  trend_str = "stable"; break;
        case PRESSURE_TREND_FALLING: trend_str = "falling"; break;
        default: break;
    }
    cJSON_AddStringToObject(pressure, "trend", trend_str);
    if (!isnan(data->metrics.pressure_delta_hpa)) cJSON_AddNumberToObject(pressure, "delta_hpa", round_to_2dp(data->metrics.pressure_delta_hpa));
    else cJSON_AddNullToObject(pressure, "delta_hpa");
    if (!isnan(data->metrics.pressure_window_hours)) cJSON_AddNumberToObject(pressure, "window_hours", round_to_1dp(data->metrics.pressure_window_hours));
    else cJSON_AddNullToObject(pressure, "window_hours");
    cJSON_AddItemToObject(root, "pressure", pressure);

    /* Scores */
    if (data->metrics.co2_score != UINT8_MAX) cJSON_AddNumberToObject(root, "co2_score", data->metrics.co2_score);
    else cJSON_AddNullToObject(root, "co2_score");
    cJSON_AddStringToObject(root, "voc_category", data->metrics.voc_category);
    cJSON_AddStringToObject(root, "nox_category", data->metrics.nox_category);
    if (data->metrics.overall_iaq_score != UINT8_MAX) cJSON_AddNumberToObject(root, "overall_iaq_score", data->metrics.overall_iaq_score);
    else cJSON_AddNullToObject(root, "overall_iaq_score");

    /* Mold */
    cJSON *mold = cJSON_CreateObject();
    if (data->metrics.mold_risk_score != UINT8_MAX) cJSON_AddNumberToObject(mold, "score", data->metrics.mold_risk_score);
    else cJSON_AddNullToObject(mold, "score");
    cJSON_AddStringToObject(mold, "category", data->metrics.mold_risk_category);
    cJSON_AddItemToObject(root, "mold_risk", mold);

    /* Trends */
    if (!isnan(data->metrics.co2_rate_ppm_hr)) cJSON_AddNumberToObject(root, "co2_rate_ppm_hr", round_to_1dp(data->metrics.co2_rate_ppm_hr));
    else cJSON_AddNullToObject(root, "co2_rate_ppm_hr");
    cJSON_AddBoolToObject(root, "pm25_spike_detected", data->metrics.pm25_spike_detected);

    return root;
}

cJSON* iaq_json_build_health(const iaq_data_t *data)
{
    if (!data) return NULL;
    cJSON *root = cJSON_CreateObject();

    /* System */
    cJSON_AddNumberToObject(root, "uptime", data->system.uptime_seconds);
    cJSON_AddNumberToObject(root, "wifi_rssi", data->system.wifi_rssi);
    cJSON_AddNumberToObject(root, "free_heap", data->system.free_heap);

    /* Time sync */
    bool ts_ok = time_sync_is_set();
    cJSON_AddBoolToObject(root, "time_synced", ts_ok);
    if (ts_ok) {
        time_t now = 0; time(&now);
        cJSON_AddNumberToObject(root, "epoch", (double)now);
    }

    /* Sensors */
    cJSON *sensors = cJSON_CreateObject();
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info((sensor_id_t)i, &info) != ESP_OK) continue;
        cJSON *sj = cJSON_CreateObject();
        cJSON_AddStringToObject(sj, "state", sensor_coordinator_state_to_string(info.state));
        cJSON_AddNumberToObject(sj, "errors", info.error_count);
        int64_t now_us = esp_timer_get_time();
        if (info.last_read_us > 0) {
            int64_t age_s = (now_us - info.last_read_us) / 1000000LL;
            cJSON_AddNumberToObject(sj, "last_read_s", (double)age_s);
        }
        if (info.state == SENSOR_STATE_WARMING) {
            int64_t remaining_us = info.warmup_deadline_us - now_us;
            if (remaining_us > 0) cJSON_AddNumberToObject(sj, "warmup_remaining_s", remaining_us / 1e6);
        }
        cJSON_AddItemToObject(sensors, sensor_coordinator_id_to_name((sensor_id_t)i), sj);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    return root;
}
