/* components/iaq_json/iaq_json.c */
#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "cJSON.h"
#include "iaq_json.h"
#include "sensor_coordinator.h"
#include "time_sync.h"
#include "power_board.h"
#include <time.h>

static double round_to_1dp(double v) { return round(v * 10.0) / 10.0; }
static double round_to_2dp(double v) { return round(v * 100.0) / 100.0; }

static void get_publishable_sensors(bool out_can_publish[SENSOR_ID_MAX])
{
    if (!out_can_publish) return;
    uint32_t cad_ms[SENSOR_ID_MAX] = {0};
    bool have_cad = (sensor_coordinator_get_cadences(cad_ms, NULL) == ESP_OK);

    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        out_can_publish[i] = false;
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info((sensor_id_t)i, &info) != ESP_OK) {
            continue;
        }
        if (info.state != SENSOR_STATE_READY) {
            continue;
        }
        if (have_cad && cad_ms[i] == 0) {
            continue;
        }
        out_can_publish[i] = true;
    }
}

cJSON* iaq_json_build_state(const iaq_data_t *data)
{
    if (!data) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    bool can_pub[SENSOR_ID_MAX] = {0};
    get_publishable_sensors(can_pub);

    /* Fused (compensated) sensor values - gate on sensor state/cadence and validity */
    if (can_pub[SENSOR_ID_SHT45] && data->valid.temp_c && !isnan(data->fused.temp_c)) cJSON_AddNumberToObject(root, "temp_c", round_to_2dp(data->fused.temp_c));
    else cJSON_AddNullToObject(root, "temp_c");

    if (can_pub[SENSOR_ID_SHT45] && data->valid.rh_pct && !isnan(data->fused.rh_pct)) cJSON_AddNumberToObject(root, "rh_pct", round_to_2dp(data->fused.rh_pct));
    else cJSON_AddNullToObject(root, "rh_pct");

    /* Convert Pa -> hPa (1 hPa = 100 Pa) */
    if (can_pub[SENSOR_ID_BMP280] && data->valid.pressure_pa && !isnan(data->fused.pressure_pa)) cJSON_AddNumberToObject(root, "pressure_hpa", round_to_2dp(data->fused.pressure_pa / 100.0));
    else cJSON_AddNullToObject(root, "pressure_hpa");

    if (can_pub[SENSOR_ID_PMS5003] && data->valid.pm25_ugm3 && !isnan(data->fused.pm25_ugm3)) cJSON_AddNumberToObject(root, "pm25_ugm3", round_to_1dp(data->fused.pm25_ugm3));
    else cJSON_AddNullToObject(root, "pm25_ugm3");

    if (can_pub[SENSOR_ID_PMS5003] && data->valid.pm10_ugm3 && !isnan(data->fused.pm10_ugm3)) cJSON_AddNumberToObject(root, "pm10_ugm3", round_to_1dp(data->fused.pm10_ugm3));
    else cJSON_AddNullToObject(root, "pm10_ugm3");

#ifdef CONFIG_MQTT_PUBLISH_PM1
    if (can_pub[SENSOR_ID_PMS5003] && data->valid.pm1_ugm3 && !isnan(data->fused.pm1_ugm3)) cJSON_AddNumberToObject(root, "pm1_ugm3", round_to_1dp(data->fused.pm1_ugm3));
    else cJSON_AddNullToObject(root, "pm1_ugm3");
#endif

    if (can_pub[SENSOR_ID_S8] && data->valid.co2_ppm && !isnan(data->fused.co2_ppm)) cJSON_AddNumberToObject(root, "co2_ppm", round(data->fused.co2_ppm));
    else cJSON_AddNullToObject(root, "co2_ppm");

    /* VOC/NOx indices (raw) */
    if (can_pub[SENSOR_ID_SGP41] && data->valid.voc_index && data->raw.voc_index != UINT16_MAX) cJSON_AddNumberToObject(root, "voc_index", data->raw.voc_index);
    else cJSON_AddNullToObject(root, "voc_index");

    if (can_pub[SENSOR_ID_SGP41] && data->valid.nox_index && data->raw.nox_index != UINT16_MAX) cJSON_AddNumberToObject(root, "nox_index", data->raw.nox_index);
    else cJSON_AddNullToObject(root, "nox_index");

    /* MCU temperature */
    if (can_pub[SENSOR_ID_MCU] && data->valid.mcu_temp_c && !isnan(data->raw.mcu_temp_c)) cJSON_AddNumberToObject(root, "mcu_temp_c", round_to_1dp(data->raw.mcu_temp_c));
    else cJSON_AddNullToObject(root, "mcu_temp_c");

    /* Basic metrics */
    if (can_pub[SENSOR_ID_PMS5003] && data->metrics.aqi_value != UINT16_MAX) cJSON_AddNumberToObject(root, "aqi", data->metrics.aqi_value);
    else cJSON_AddNullToObject(root, "aqi");

    /* Emit comfort_score when valid (UINT8_MAX denotes unknown) */
    if (can_pub[SENSOR_ID_SHT45] && data->metrics.comfort_score != UINT8_MAX) cJSON_AddNumberToObject(root, "comfort_score", data->metrics.comfort_score);
    else cJSON_AddNullToObject(root, "comfort_score");

    /* Last known valid fused values (for stale display in frontend) */
    cJSON *last = cJSON_CreateObject();
    if (!last) { cJSON_Delete(root); return NULL; }

    /* Temperature & humidity from SHT45 */
    if (data->updated_at.sht45 > 0 && !isnan(data->fused.temp_c)) {
        cJSON_AddNumberToObject(last, "temp_c", round_to_2dp(data->fused.temp_c));
    } else {
        cJSON_AddNullToObject(last, "temp_c");
    }

    if (data->updated_at.sht45 > 0 && !isnan(data->fused.rh_pct)) {
        cJSON_AddNumberToObject(last, "rh_pct", round_to_2dp(data->fused.rh_pct));
    } else {
        cJSON_AddNullToObject(last, "rh_pct");
    }

    /* Pressure from BMP280 */
    if (data->updated_at.bmp280 > 0 && !isnan(data->fused.pressure_pa)) {
        cJSON_AddNumberToObject(last, "pressure_hpa", round_to_2dp(data->fused.pressure_pa / 100.0));
    } else {
        cJSON_AddNullToObject(last, "pressure_hpa");
    }

    /* PM from PMS5003 */
    if (data->updated_at.pms5003 > 0 && !isnan(data->fused.pm25_ugm3)) {
        cJSON_AddNumberToObject(last, "pm25_ugm3", round_to_1dp(data->fused.pm25_ugm3));
    } else {
        cJSON_AddNullToObject(last, "pm25_ugm3");
    }

    if (data->updated_at.pms5003 > 0 && !isnan(data->fused.pm10_ugm3)) {
        cJSON_AddNumberToObject(last, "pm10_ugm3", round_to_1dp(data->fused.pm10_ugm3));
    } else {
        cJSON_AddNullToObject(last, "pm10_ugm3");
    }

#ifdef CONFIG_MQTT_PUBLISH_PM1
    if (data->updated_at.pms5003 > 0 && !isnan(data->fused.pm1_ugm3)) {
        cJSON_AddNumberToObject(last, "pm1_ugm3", round_to_1dp(data->fused.pm1_ugm3));
    } else {
        cJSON_AddNullToObject(last, "pm1_ugm3");
    }
#endif

    /* CO2 from S8 */
    if (data->updated_at.s8 > 0 && !isnan(data->fused.co2_ppm)) {
        cJSON_AddNumberToObject(last, "co2_ppm", round(data->fused.co2_ppm));
    } else {
        cJSON_AddNullToObject(last, "co2_ppm");
    }

    /* VOC/NOx indices from SGP41 */
    if (data->updated_at.sgp41 > 0 && data->raw.voc_index != UINT16_MAX) {
        cJSON_AddNumberToObject(last, "voc_index", data->raw.voc_index);
    } else {
        cJSON_AddNullToObject(last, "voc_index");
    }

    if (data->updated_at.sgp41 > 0 && data->raw.nox_index != UINT16_MAX) {
        cJSON_AddNumberToObject(last, "nox_index", data->raw.nox_index);
    } else {
        cJSON_AddNullToObject(last, "nox_index");
    }

    /* MCU temperature */
    if (data->updated_at.mcu > 0 && !isnan(data->raw.mcu_temp_c)) {
        cJSON_AddNumberToObject(last, "mcu_temp_c", round_to_1dp(data->raw.mcu_temp_c));
    } else {
        cJSON_AddNullToObject(last, "mcu_temp_c");
    }

    cJSON_AddItemToObject(root, "last", last);

    return root;
}

cJSON* iaq_json_build_metrics(const iaq_data_t *data)
{
    if (!data) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    bool can_pub[SENSOR_ID_MAX] = {0};
    get_publishable_sensors(can_pub);
    const bool pm_ok = can_pub[SENSOR_ID_PMS5003];
    const bool sht_ok = can_pub[SENSOR_ID_SHT45];
    const bool bmp_ok = can_pub[SENSOR_ID_BMP280];
    const bool s8_ok = can_pub[SENSOR_ID_S8];
    const bool sgp_ok = can_pub[SENSOR_ID_SGP41];
    const bool iaq_ok = pm_ok && s8_ok && sht_ok;

    /* AQI breakdown */
    cJSON *aqi = cJSON_CreateObject();
    if (!aqi) { cJSON_Delete(root); return NULL; }
    if (pm_ok && data->metrics.aqi_value != UINT16_MAX) cJSON_AddNumberToObject(aqi, "value", data->metrics.aqi_value);
    else cJSON_AddNullToObject(aqi, "value");
    if (pm_ok && data->metrics.aqi_value != UINT16_MAX) cJSON_AddStringToObject(aqi, "category", data->metrics.aqi_category);
    else cJSON_AddNullToObject(aqi, "category");
    if (pm_ok && data->metrics.aqi_value != UINT16_MAX) cJSON_AddStringToObject(aqi, "dominant", data->metrics.aqi_dominant);
    else cJSON_AddNullToObject(aqi, "dominant");
    if (pm_ok && !isnan(data->metrics.aqi_pm25_subindex)) cJSON_AddNumberToObject(aqi, "pm25_subindex", round_to_1dp(data->metrics.aqi_pm25_subindex));
    else cJSON_AddNullToObject(aqi, "pm25_subindex");
    if (pm_ok && !isnan(data->metrics.aqi_pm10_subindex)) cJSON_AddNumberToObject(aqi, "pm10_subindex", round_to_1dp(data->metrics.aqi_pm10_subindex));
    else cJSON_AddNullToObject(aqi, "pm10_subindex");
    cJSON_AddItemToObject(root, "aqi", aqi);

    /* Comfort */
    cJSON *comfort = cJSON_CreateObject();
    if (!comfort) { cJSON_Delete(root); return NULL; }
    if (sht_ok && data->metrics.comfort_score != UINT8_MAX) cJSON_AddNumberToObject(comfort, "score", data->metrics.comfort_score);
    else cJSON_AddNullToObject(comfort, "score");
    if (sht_ok && data->metrics.comfort_score != UINT8_MAX) cJSON_AddStringToObject(comfort, "category", data->metrics.comfort_category);
    else cJSON_AddNullToObject(comfort, "category");
    if (sht_ok && !isnan(data->metrics.dew_point_c)) cJSON_AddNumberToObject(comfort, "dew_point_c", round_to_1dp(data->metrics.dew_point_c));
    else cJSON_AddNullToObject(comfort, "dew_point_c");
    if (sht_ok && !isnan(data->metrics.abs_humidity_gm3)) cJSON_AddNumberToObject(comfort, "abs_humidity_gm3", round_to_1dp(data->metrics.abs_humidity_gm3));
    else cJSON_AddNullToObject(comfort, "abs_humidity_gm3");
    if (sht_ok && !isnan(data->metrics.heat_index_c)) cJSON_AddNumberToObject(comfort, "heat_index_c", round_to_1dp(data->metrics.heat_index_c));
    else cJSON_AddNullToObject(comfort, "heat_index_c");
    cJSON_AddItemToObject(root, "comfort", comfort);

    /* Pressure */
    cJSON *pressure = cJSON_CreateObject();
    if (!pressure) { cJSON_Delete(root); return NULL; }
    if (bmp_ok) {
        const char *trend_str = "unknown";
        switch (data->metrics.pressure_trend) {
            case PRESSURE_TREND_RISING:  trend_str = "rising"; break;
            case PRESSURE_TREND_STABLE:  trend_str = "stable"; break;
            case PRESSURE_TREND_FALLING: trend_str = "falling"; break;
            default: break;
        }
        cJSON_AddStringToObject(pressure, "trend", trend_str);
    } else {
        cJSON_AddNullToObject(pressure, "trend");
    }
    if (bmp_ok && !isnan(data->metrics.pressure_delta_hpa)) cJSON_AddNumberToObject(pressure, "delta_hpa", round_to_2dp(data->metrics.pressure_delta_hpa));
    else cJSON_AddNullToObject(pressure, "delta_hpa");
    if (bmp_ok && !isnan(data->metrics.pressure_window_hours)) cJSON_AddNumberToObject(pressure, "window_hours", round_to_1dp(data->metrics.pressure_window_hours));
    else cJSON_AddNullToObject(pressure, "window_hours");
    cJSON_AddItemToObject(root, "pressure", pressure);

    /* Scores */
    if (s8_ok && data->metrics.co2_score != UINT8_MAX) cJSON_AddNumberToObject(root, "co2_score", data->metrics.co2_score);
    else cJSON_AddNullToObject(root, "co2_score");
    if (sgp_ok) cJSON_AddStringToObject(root, "voc_category", data->metrics.voc_category);
    else cJSON_AddNullToObject(root, "voc_category");
    if (sgp_ok) cJSON_AddStringToObject(root, "nox_category", data->metrics.nox_category);
    else cJSON_AddNullToObject(root, "nox_category");
    if (iaq_ok && data->metrics.overall_iaq_score != UINT8_MAX) cJSON_AddNumberToObject(root, "overall_iaq_score", data->metrics.overall_iaq_score);
    else cJSON_AddNullToObject(root, "overall_iaq_score");

    /* Mold */
    cJSON *mold = cJSON_CreateObject();
    if (!mold) { cJSON_Delete(root); return NULL; }
    if (sht_ok && data->metrics.mold_risk_score != UINT8_MAX) cJSON_AddNumberToObject(mold, "score", data->metrics.mold_risk_score);
    else cJSON_AddNullToObject(mold, "score");
    if (sht_ok && data->metrics.mold_risk_score != UINT8_MAX) cJSON_AddStringToObject(mold, "category", data->metrics.mold_risk_category);
    else cJSON_AddNullToObject(mold, "category");
    cJSON_AddItemToObject(root, "mold_risk", mold);

    /* Trends */
    if (s8_ok && !isnan(data->metrics.co2_rate_ppm_hr)) cJSON_AddNumberToObject(root, "co2_rate_ppm_hr", round_to_1dp(data->metrics.co2_rate_ppm_hr));
    else cJSON_AddNullToObject(root, "co2_rate_ppm_hr");
    if (pm_ok) cJSON_AddBoolToObject(root, "pm25_spike_detected", data->metrics.pm25_spike_detected);
    else cJSON_AddNullToObject(root, "pm25_spike_detected");

    /* Last known valid metrics (for stale display in frontend).
     * Gate only on value validity, not sensor publish state.
     * This allows frontend to show last reading when sensors are disabled/stale. */
    cJSON *last = cJSON_CreateObject();
    if (!last) { cJSON_Delete(root); return NULL; }

    /* AQI breakdown - gate on updated_at.pms5003 (we had PM data at some point) */
    cJSON *last_aqi = cJSON_CreateObject();
    if (!last_aqi) { cJSON_Delete(root); return NULL; }
    if (data->updated_at.pms5003 > 0 && data->metrics.aqi_value != UINT16_MAX) {
        cJSON_AddNumberToObject(last_aqi, "value", data->metrics.aqi_value);
        cJSON_AddStringToObject(last_aqi, "category", data->metrics.aqi_category);
        cJSON_AddStringToObject(last_aqi, "dominant", data->metrics.aqi_dominant);
        if (!isnan(data->metrics.aqi_pm25_subindex)) cJSON_AddNumberToObject(last_aqi, "pm25_subindex", round_to_1dp(data->metrics.aqi_pm25_subindex));
        else cJSON_AddNullToObject(last_aqi, "pm25_subindex");
        if (!isnan(data->metrics.aqi_pm10_subindex)) cJSON_AddNumberToObject(last_aqi, "pm10_subindex", round_to_1dp(data->metrics.aqi_pm10_subindex));
        else cJSON_AddNullToObject(last_aqi, "pm10_subindex");
    } else {
        cJSON_AddNullToObject(last_aqi, "value");
        cJSON_AddNullToObject(last_aqi, "category");
        cJSON_AddNullToObject(last_aqi, "dominant");
        cJSON_AddNullToObject(last_aqi, "pm25_subindex");
        cJSON_AddNullToObject(last_aqi, "pm10_subindex");
    }
    cJSON_AddItemToObject(last, "aqi", last_aqi);

    /* Comfort - gate on updated_at.sht45 */
    cJSON *last_comfort = cJSON_CreateObject();
    if (!last_comfort) { cJSON_Delete(root); return NULL; }
    if (data->updated_at.sht45 > 0 && data->metrics.comfort_score != UINT8_MAX) {
        cJSON_AddNumberToObject(last_comfort, "score", data->metrics.comfort_score);
        cJSON_AddStringToObject(last_comfort, "category", data->metrics.comfort_category);
        if (!isnan(data->metrics.dew_point_c)) cJSON_AddNumberToObject(last_comfort, "dew_point_c", round_to_1dp(data->metrics.dew_point_c));
        else cJSON_AddNullToObject(last_comfort, "dew_point_c");
        if (!isnan(data->metrics.abs_humidity_gm3)) cJSON_AddNumberToObject(last_comfort, "abs_humidity_gm3", round_to_1dp(data->metrics.abs_humidity_gm3));
        else cJSON_AddNullToObject(last_comfort, "abs_humidity_gm3");
        if (!isnan(data->metrics.heat_index_c)) cJSON_AddNumberToObject(last_comfort, "heat_index_c", round_to_1dp(data->metrics.heat_index_c));
        else cJSON_AddNullToObject(last_comfort, "heat_index_c");
    } else {
        cJSON_AddNullToObject(last_comfort, "score");
        cJSON_AddNullToObject(last_comfort, "category");
        cJSON_AddNullToObject(last_comfort, "dew_point_c");
        cJSON_AddNullToObject(last_comfort, "abs_humidity_gm3");
        cJSON_AddNullToObject(last_comfort, "heat_index_c");
    }
    cJSON_AddItemToObject(last, "comfort", last_comfort);

    /* Pressure - gate on updated_at.bmp280 */
    cJSON *last_pressure = cJSON_CreateObject();
    if (!last_pressure) { cJSON_Delete(root); return NULL; }
    if (data->updated_at.bmp280 > 0) {
        const char *trend_str = "unknown";
        switch (data->metrics.pressure_trend) {
            case PRESSURE_TREND_RISING:  trend_str = "rising"; break;
            case PRESSURE_TREND_STABLE:  trend_str = "stable"; break;
            case PRESSURE_TREND_FALLING: trend_str = "falling"; break;
            default: break;
        }
        cJSON_AddStringToObject(last_pressure, "trend", trend_str);
        if (!isnan(data->metrics.pressure_delta_hpa)) cJSON_AddNumberToObject(last_pressure, "delta_hpa", round_to_2dp(data->metrics.pressure_delta_hpa));
        else cJSON_AddNullToObject(last_pressure, "delta_hpa");
        if (!isnan(data->metrics.pressure_window_hours)) cJSON_AddNumberToObject(last_pressure, "window_hours", round_to_1dp(data->metrics.pressure_window_hours));
        else cJSON_AddNullToObject(last_pressure, "window_hours");
    } else {
        cJSON_AddNullToObject(last_pressure, "trend");
        cJSON_AddNullToObject(last_pressure, "delta_hpa");
        cJSON_AddNullToObject(last_pressure, "window_hours");
    }
    cJSON_AddItemToObject(last, "pressure", last_pressure);

    /* CO2 score - gate on updated_at.s8 */
    if (data->updated_at.s8 > 0 && data->metrics.co2_score != UINT8_MAX) cJSON_AddNumberToObject(last, "co2_score", data->metrics.co2_score);
    else cJSON_AddNullToObject(last, "co2_score");

    /* VOC/NOx categories - gate on updated_at.sgp41 */
    if (data->updated_at.sgp41 > 0) {
        cJSON_AddStringToObject(last, "voc_category", data->metrics.voc_category);
        cJSON_AddStringToObject(last, "nox_category", data->metrics.nox_category);
    } else {
        cJSON_AddNullToObject(last, "voc_category");
        cJSON_AddNullToObject(last, "nox_category");
    }

    /* Overall IAQ - requires PM, S8, and SHT45 data */
    bool had_iaq_deps = data->updated_at.pms5003 > 0 && data->updated_at.s8 > 0 && data->updated_at.sht45 > 0;
    if (had_iaq_deps && data->metrics.overall_iaq_score != UINT8_MAX) cJSON_AddNumberToObject(last, "overall_iaq_score", data->metrics.overall_iaq_score);
    else cJSON_AddNullToObject(last, "overall_iaq_score");

    /* Mold risk - gate on updated_at.sht45 */
    cJSON *last_mold = cJSON_CreateObject();
    if (!last_mold) { cJSON_Delete(root); return NULL; }
    if (data->updated_at.sht45 > 0 && data->metrics.mold_risk_score != UINT8_MAX) {
        cJSON_AddNumberToObject(last_mold, "score", data->metrics.mold_risk_score);
        cJSON_AddStringToObject(last_mold, "category", data->metrics.mold_risk_category);
    } else {
        cJSON_AddNullToObject(last_mold, "score");
        cJSON_AddNullToObject(last_mold, "category");
    }
    cJSON_AddItemToObject(last, "mold_risk", last_mold);

    /* CO2 rate - gate on updated_at.s8 */
    if (data->updated_at.s8 > 0 && !isnan(data->metrics.co2_rate_ppm_hr)) cJSON_AddNumberToObject(last, "co2_rate_ppm_hr", round_to_1dp(data->metrics.co2_rate_ppm_hr));
    else cJSON_AddNullToObject(last, "co2_rate_ppm_hr");

    /* PM spike - gate on updated_at.pms5003 */
    if (data->updated_at.pms5003 > 0) cJSON_AddBoolToObject(last, "pm25_spike_detected", data->metrics.pm25_spike_detected);
    else cJSON_AddNullToObject(last, "pm25_spike_detected");

    cJSON_AddItemToObject(root, "last", last);

    return root;
}

cJSON* iaq_json_build_health(const iaq_data_t *data)
{
    if (!data) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* System */
    cJSON_AddNumberToObject(root, "uptime", data->system.uptime_seconds);
    cJSON_AddNumberToObject(root, "wifi_rssi", data->system.wifi_rssi);
    cJSON_AddNumberToObject(root, "internal_free", data->system.internal_free);
    cJSON_AddNumberToObject(root, "internal_total", data->system.internal_total);
    cJSON_AddNumberToObject(root, "spiram_free", data->system.spiram_free);
    cJSON_AddNumberToObject(root, "spiram_total", data->system.spiram_total);

    /* Time sync */
    bool ts_ok = time_sync_is_set();
    cJSON_AddBoolToObject(root, "time_synced", ts_ok);
    if (ts_ok) {
        time_t now = 0; time(&now);
        cJSON_AddNumberToObject(root, "epoch", (double)now);
    }

    /* Sensors */
    cJSON *sensors = cJSON_CreateObject();
    if (!sensors) { cJSON_Delete(root); return NULL; }
    uint32_t cad_ms[SENSOR_ID_MAX] = {0};
    if (sensor_coordinator_get_cadences(cad_ms, NULL) != ESP_OK) {
        for (int i = 0; i < SENSOR_ID_MAX; ++i) cad_ms[i] = 0;
    }
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info((sensor_id_t)i, &info) != ESP_OK) continue;
        cJSON *sj = cJSON_CreateObject();
        if (!sj) continue;
        cJSON_AddStringToObject(sj, "state", sensor_coordinator_state_to_string(info.state));
        cJSON_AddNumberToObject(sj, "errors", info.error_count);
        int64_t now_us = esp_timer_get_time();
        bool has_age = false;
        double age_s = 0.0;
        if (info.last_read_us > 0) {
            int64_t age_whole_s = (now_us - info.last_read_us) / 1000000LL;
            age_s = (double)age_whole_s;
            has_age = true;
            cJSON_AddNumberToObject(sj, "last_read_s", (double)age_whole_s);
        }
        if (info.state == SENSOR_STATE_WARMING) {
            int64_t remaining_us = info.warmup_deadline_us - now_us;
            if (remaining_us > 0) cJSON_AddNumberToObject(sj, "warmup_remaining_s", remaining_us / 1e6);
        }

        /* Backend-derived staleness: true when we have a prior reading and it is overdue
         * based on configured cadence (with a safety factor). */
        bool stale = false;
        uint32_t interval_ms = cad_ms[i];
        if (has_age && interval_ms > 0) {
            double expected_s = (double)interval_ms / 1000.0;
            double threshold_s = expected_s * 2.5;  /* 2.5x cadence as reserve */
            if (threshold_s < 10.0) threshold_s = 10.0; /* minimum grace */
            if (age_s > threshold_s) {
                stale = true;
            }
        }
        cJSON_AddBoolToObject(sj, "stale", stale);

        cJSON_AddItemToObject(sensors, sensor_coordinator_id_to_name((sensor_id_t)i), sj);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    return root;
}

cJSON* iaq_json_build_power(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

#if !CONFIG_IAQ_POWERFEATHER_ENABLE
    cJSON_AddBoolToObject(root, "available", false);
    return root;
#else
    iaq_power_snapshot_t p = {0};
    IAQ_DATA_WITH_LOCK() { p = iaq_data_get()->power; }

    cJSON_AddBoolToObject(root, "available", p.available);
    if (!p.available) {
        return root;
    }

    cJSON_AddBoolToObject(root, "supply_good", p.supply_good);
    cJSON_AddNumberToObject(root, "supply_mv", p.supply_mv);
    cJSON_AddNumberToObject(root, "supply_ma", p.supply_ma);
    cJSON_AddNumberToObject(root, "maintain_mv", p.maintain_mv);
    cJSON_AddBoolToObject(root, "en", p.en);
    cJSON_AddBoolToObject(root, "v3v_on", p.v3v_on);
    cJSON_AddBoolToObject(root, "vsqt_on", p.vsqt_on);
    cJSON_AddBoolToObject(root, "stat_on", p.stat_on);
    cJSON_AddBoolToObject(root, "charging_on", p.charging_on);
    cJSON_AddNumberToObject(root, "charge_limit_ma", p.charge_limit_ma);
    cJSON_AddNumberToObject(root, "batt_mv", p.batt_mv);
    cJSON_AddNumberToObject(root, "batt_ma", p.batt_ma);
    cJSON_AddNumberToObject(root, "charge_pct", p.charge_pct);
    cJSON_AddNumberToObject(root, "health_pct", p.health_pct);
    cJSON_AddNumberToObject(root, "cycles", p.cycles);
    cJSON_AddNumberToObject(root, "time_left_min", p.time_left_min);
    cJSON_AddNumberToObject(root, "batt_temp_c", p.batt_temp_c);
    cJSON_AddNumberToObject(root, "alarm_low_v_mv", p.alarm_low_v_mv);
    cJSON_AddNumberToObject(root, "alarm_high_v_mv", p.alarm_high_v_mv);
    cJSON_AddNumberToObject(root, "alarm_low_pct", p.alarm_low_pct);
    cJSON_AddNumberToObject(root, "updated_at_us", (double)p.updated_us);
    return root;
#endif
}
