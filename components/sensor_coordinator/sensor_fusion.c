/* components/sensor_coordinator/sensor_fusion.c */
#include "sensor_fusion.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "iaq_config.h"

static const char *TAG = "FUSION";

/* NVS namespace for fusion configuration */
#define FUSION_NVS_NAMESPACE "fusion_cfg"

/* PM RH correction coefficients */
static float s_pm_rh_a = 0.0f;
static float s_pm_rh_b = 0.0f;

/* Temperature self-heating offset */
static float s_temp_offset_c = 0.0f;

/* CO2 ABC baseline tracking */
#define ABC_HISTORY_DAYS 7
typedef struct {
    uint16_t baseline_ppm;        /* Current baseline (400 = outdoor air) */
    uint8_t confidence_pct;       /* Confidence 0-100 based on # nights */
    float nightly_minima[ABC_HISTORY_DAYS];  /* Ring buffer of nightly CO2 lows */
    uint8_t minima_count;         /* Number of valid entries */
    uint8_t minima_head;          /* Ring buffer head index */
    int64_t last_update_us;       /* Last ABC update timestamp */
    float daily_minimum;          /* Current day's minimum CO2 */
    bool in_night_window;         /* Are we in the ABC tracking window? */
} abc_state_t;

static abc_state_t s_abc_state = {
    .baseline_ppm = 400,
    .confidence_pct = 0,
    .minima_count = 0,
    .minima_head = 0,
    .last_update_us = 0,
    .daily_minimum = INFINITY,
    .in_night_window = false
};

/* Helper: Load float from NVS as string (Kconfig stores floats as strings) */
static float load_float_from_nvs(nvs_handle_t h, const char *key, float default_val)
{
    char buf[16];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, key, buf, &len) == ESP_OK) {
        return atof(buf);
    }
    return default_val;
}

/* Helper: Save float to NVS as string */
static esp_err_t save_float_to_nvs(nvs_handle_t h, const char *key, float value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.4f", value);
    return nvs_set_str(h, key, buf);
}

esp_err_t fusion_init(void)
{
    ESP_LOGI(TAG, "Initializing sensor fusion");

    nvs_handle_t h;
    esp_err_t err = nvs_open(FUSION_NVS_NAMESPACE, NVS_READWRITE, &h);

    if (err == ESP_OK) {
        /* Load PM RH coefficients (or use Kconfig defaults) */
        s_pm_rh_a = load_float_from_nvs(h, "pm_rh_a", atof(CONFIG_FUSION_PM_RH_A));
        s_pm_rh_b = load_float_from_nvs(h, "pm_rh_b", atof(CONFIG_FUSION_PM_RH_B));

        /* Load temperature offset */
        s_temp_offset_c = load_float_from_nvs(h, "temp_offset", atof(CONFIG_FUSION_TEMP_SELF_HEAT_OFFSET_C));

#ifdef CONFIG_FUSION_CO2_ABC_ENABLE
        /* Load ABC baseline (or default to outdoor air) */
        uint16_t baseline = 400;
        if (nvs_get_u16(h, "abc_baseline", &baseline) == ESP_OK) {
            s_abc_state.baseline_ppm = baseline;
            /* Assume some confidence if we loaded from NVS */
            s_abc_state.confidence_pct = 50;
            ESP_LOGI(TAG, "Loaded CO2 ABC baseline: %u ppm", baseline);
        }
#endif

        nvs_close(h);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot - use Kconfig defaults */
        s_pm_rh_a = atof(CONFIG_FUSION_PM_RH_A);
        s_pm_rh_b = atof(CONFIG_FUSION_PM_RH_B);
        s_temp_offset_c = atof(CONFIG_FUSION_TEMP_SELF_HEAT_OFFSET_C);
        ESP_LOGI(TAG, "Using Kconfig defaults (NVS not found)");
    } else {
        ESP_LOGW(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        /* Use Kconfig defaults as fallback */
        s_pm_rh_a = atof(CONFIG_FUSION_PM_RH_A);
        s_pm_rh_b = atof(CONFIG_FUSION_PM_RH_B);
        s_temp_offset_c = atof(CONFIG_FUSION_TEMP_SELF_HEAT_OFFSET_C);
    }

    ESP_LOGI(TAG, "PM RH coeffs: a=%.3f, b=%.3f", s_pm_rh_a, s_pm_rh_b);
    ESP_LOGI(TAG, "Temp offset: %.2f C", s_temp_offset_c);

    return ESP_OK;
}

/**
 * Apply PM humidity correction.
 * Formula: pm_dry = pm_raw / (1 + a*(RH/100)^b)
 *
 * Guards:
 * - RH must be valid and < CONFIG_FUSION_PM_RH_MAX_PERCENT
 * - PM values must be valid (not NaN)
 */
static void apply_pm_rh_correction(iaq_data_t *data)
{
#ifdef CONFIG_FUSION_PM_RH_ENABLE
    if (!data->valid.humidity || !data->valid.pm2_5) {
        /* Copy raw values to fused (no correction possible) */
        data->fused.pm1_ugm3 = data->pm1_0;
        data->fused.pm25_ugm3 = data->pm2_5;
        data->fused.pm10_ugm3 = data->pm10;
        data->fusion_diag.pm_rh_factor = 1.0f;
        data->fusion_diag.pm25_quality = 0;
        return;
    }

    float rh = data->humidity;

    /* If RH too high, don't apply correction (sensor unreliable) */
    if (rh >= CONFIG_FUSION_PM_RH_MAX_PERCENT) {
        data->fused.pm1_ugm3 = data->pm1_0;
        data->fused.pm25_ugm3 = data->pm2_5;
        data->fused.pm10_ugm3 = data->pm10;
        data->fusion_diag.pm_rh_factor = 1.0f;
        data->fusion_diag.pm25_quality = 20;  /* Low quality at high RH */
        ESP_LOGD(TAG, "PM RH correction skipped (RH=%.1f%% > %d%%)", rh, CONFIG_FUSION_PM_RH_MAX_PERCENT);
        return;
    }

    /* Calculate correction factor: 1 + a*(RH/100)^b */
    float rh_normalized = rh / 100.0f;
    float correction_factor = 1.0f + s_pm_rh_a * powf(rh_normalized, s_pm_rh_b);

    /* Apply correction */
    data->fused.pm1_ugm3 = data->pm1_0 / correction_factor;
    data->fused.pm25_ugm3 = data->pm2_5 / correction_factor;
    data->fused.pm10_ugm3 = data->pm10 / correction_factor;

    /* Calculate PM1/PM2.5 ratio for sensor health check */
    if (data->fused.pm25_ugm3 > 1.0f) {
        data->fusion_diag.pm1_pm25_ratio = data->fused.pm1_ugm3 / data->fused.pm25_ugm3;
    } else {
        data->fusion_diag.pm1_pm25_ratio = NAN;
    }

    /* Calculate quality score (100 at low RH, degraded at high RH) */
    if (rh < 60.0f) {
        data->fusion_diag.pm25_quality = 100;
    } else if (rh < 80.0f) {
        data->fusion_diag.pm25_quality = 100 - (uint8_t)((rh - 60.0f) * 2.0f);  /* 100 -> 60 */
    } else {
        data->fusion_diag.pm25_quality = 60 - (uint8_t)((rh - 80.0f) * 4.0f);  /* 60 -> 20 */
    }

    data->fusion_diag.pm_rh_factor = correction_factor;

    ESP_LOGD(TAG, "PM RH correction: factor=%.3f, PM2.5: %.1f -> %.1f ug/m3",
             correction_factor, data->pm2_5, data->fused.pm25_ugm3);
#else
    /* PM RH correction disabled - copy raw to fused */
    data->fused.pm1_ugm3 = data->pm1_0;
    data->fused.pm25_ugm3 = data->pm2_5;
    data->fused.pm10_ugm3 = data->pm10;
    data->fusion_diag.pm_rh_factor = 1.0f;
    data->fusion_diag.pm25_quality = 100;

    /* Still calculate PM1/PM2.5 ratio for diagnostics */
    if (data->pm2_5 > 1.0f) {
        data->fusion_diag.pm1_pm25_ratio = data->pm1_0 / data->pm2_5;
    } else {
        data->fusion_diag.pm1_pm25_ratio = NAN;
    }
#endif
}

/**
 * Apply CO2 pressure compensation.
 * Formula: co2_corrected = co2_raw * (P_ref / P_measured)
 *
 * Guards:
 * - Pressure must be valid and within reasonable range (95000-106000 Pa)
 * - CO2 must be valid
 */
static void apply_co2_pressure_compensation(iaq_data_t *data)
{
#ifdef CONFIG_FUSION_CO2_PRESSURE_ENABLE
    if (!data->valid.co2_ppm || !data->valid.pressure) {
        /* No compensation possible */
        data->fusion_diag.co2_pressure_offset_ppm = 0.0f;
        return;
    }

    float pressure_pa = data->pressure * 100.0f;  /* hPa -> Pa */

    /* Sanity check pressure (95-106 kPa) */
    if (pressure_pa < 95000.0f || pressure_pa > 106000.0f) {
        ESP_LOGW(TAG, "Pressure out of range: %.0f Pa (skipping CO2 compensation)", pressure_pa);
        data->fusion_diag.co2_pressure_offset_ppm = 0.0f;
        return;
    }

    float co2_raw = data->co2_ppm;
    float p_ref = (float)CONFIG_FUSION_CO2_PRESSURE_REF_PA;
    float co2_compensated = co2_raw * (p_ref / pressure_pa);

    data->fusion_diag.co2_pressure_offset_ppm = co2_compensated - co2_raw;

    /* Update fused CO2 (will be further adjusted by ABC if enabled) */
    data->fused.co2_ppm = co2_compensated;

    ESP_LOGD(TAG, "CO2 pressure comp: %.0f -> %.0f ppm (offset: %.1f ppm)",
             co2_raw, co2_compensated, data->fusion_diag.co2_pressure_offset_ppm);
#else
    /* Pressure compensation disabled */
    data->fusion_diag.co2_pressure_offset_ppm = 0.0f;
    if (data->valid.co2_ppm) {
        data->fused.co2_ppm = data->co2_ppm;
    }
#endif
}

/**
 * Apply temperature self-heating correction.
 * Simple offset subtraction: temp_ambient = temp_sensor - offset
 */
static void apply_temp_self_heat_correction(iaq_data_t *data)
{
    if (!data->valid.temperature) {
        return;
    }

    float temp_compensated = data->temperature - s_temp_offset_c;
    data->fused.temp_c = temp_compensated;
    data->fusion_diag.temp_self_heat_offset_c = s_temp_offset_c;

    if (fabs(s_temp_offset_c) > 0.01f) {
        ESP_LOGD(TAG, "Temp self-heat correction: %.2f -> %.2f C (offset: %.2f C)",
                 data->temperature, temp_compensated, s_temp_offset_c);
    }
}

/**
 * Apply humidity passthrough (no correction needed).
 */
static void apply_humidity_passthrough(iaq_data_t *data)
{
    if (data->valid.humidity) {
        data->fused.rh_pct = data->humidity;
    }
}

/**
 * Apply pressure passthrough (used as reference for other compensations).
 */
static void apply_pressure_passthrough(iaq_data_t *data)
{
    if (data->valid.pressure) {
        data->fused.pressure_pa = data->pressure * 100.0f;  /* hPa -> Pa */
    }
}

void fusion_update_abc(float co2_ppm, int64_t timestamp_us)
{
#ifdef CONFIG_FUSION_CO2_ABC_ENABLE
    /* Get current hour (requires RTC or NTP - stub for now) */
    /* TODO: Integrate with system time when RTC/NTP available */
    /* For now, use uptime modulo to simulate day/night cycle for testing */
    uint32_t uptime_hours = (uint32_t)(timestamp_us / (3600LL * 1000000LL));
    uint8_t hour_of_day = uptime_hours % 24;

    bool in_night_window = (hour_of_day >= CONFIG_FUSION_CO2_ABC_NIGHT_START_HOUR &&
                            hour_of_day < CONFIG_FUSION_CO2_ABC_NIGHT_END_HOUR);

    /* Track daily minimum during night window */
    if (in_night_window) {
        if (!s_abc_state.in_night_window) {
            /* Entering night window - reset daily minimum */
            s_abc_state.daily_minimum = co2_ppm;
            s_abc_state.in_night_window = true;
        } else {
            /* Update daily minimum */
            if (co2_ppm < s_abc_state.daily_minimum) {
                s_abc_state.daily_minimum = co2_ppm;
            }
        }
    } else if (s_abc_state.in_night_window) {
        /* Exiting night window - record daily minimum */
        s_abc_state.in_night_window = false;

        if (s_abc_state.daily_minimum < 2000.0f && s_abc_state.daily_minimum > 300.0f) {
            /* Add to ring buffer */
            s_abc_state.nightly_minima[s_abc_state.minima_head] = s_abc_state.daily_minimum;
            s_abc_state.minima_head = (s_abc_state.minima_head + 1) % ABC_HISTORY_DAYS;
            if (s_abc_state.minima_count < ABC_HISTORY_DAYS) {
                s_abc_state.minima_count++;
            }

            /* Calculate new baseline (average of nightly minima) */
            float sum = 0.0f;
            for (uint8_t i = 0; i < s_abc_state.minima_count; i++) {
                sum += s_abc_state.nightly_minima[i];
            }
            s_abc_state.baseline_ppm = (uint16_t)(sum / s_abc_state.minima_count);

            /* Update confidence based on # nights tracked */
            s_abc_state.confidence_pct = (s_abc_state.minima_count * 100) / ABC_HISTORY_DAYS;

            ESP_LOGI(TAG, "ABC baseline updated: %u ppm (confidence: %u%%, nights: %u)",
                     s_abc_state.baseline_ppm, s_abc_state.confidence_pct, s_abc_state.minima_count);

            /* Persist to NVS */
            nvs_handle_t h;
            if (nvs_open(FUSION_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_u16(h, "abc_baseline", s_abc_state.baseline_ppm);
                nvs_commit(h);
                nvs_close(h);
            }
        }

        /* Reset daily minimum for next cycle */
        s_abc_state.daily_minimum = INFINITY;
    }

    s_abc_state.last_update_us = timestamp_us;
#endif
}

/**
 * Apply CO2 ABC baseline correction.
 * Only applies if confidence is high enough (>= 70%).
 */
static void apply_co2_abc_correction(iaq_data_t *data, int64_t timestamp_us)
{
#ifdef CONFIG_FUSION_CO2_ABC_ENABLE
    if (!data->valid.co2_ppm) {
        return;
    }

    /* Update ABC tracking with current CO2 value */
    fusion_update_abc(data->fused.co2_ppm, timestamp_us);

    /* Apply ABC correction only if confidence is sufficient */
    if (s_abc_state.confidence_pct >= 70) {
        /* Calculate offset from expected outdoor baseline */
        float abc_offset = 400.0f - s_abc_state.baseline_ppm;
        data->fused.co2_ppm += abc_offset;

        ESP_LOGD(TAG, "CO2 ABC correction applied: offset=%.0f ppm (baseline=%u ppm, conf=%u%%)",
                 abc_offset, s_abc_state.baseline_ppm, s_abc_state.confidence_pct);
    }

    /* Update diagnostics */
    data->fusion_diag.co2_abc_baseline_ppm = s_abc_state.baseline_ppm;
    data->fusion_diag.co2_abc_confidence_pct = s_abc_state.confidence_pct;
#else
    /* ABC disabled */
    data->fusion_diag.co2_abc_baseline_ppm = 400;
    data->fusion_diag.co2_abc_confidence_pct = 0;
#endif
}

void fusion_apply(iaq_data_t *data)
{
    if (!data) {
        return;
    }

    /* Get current timestamp for ABC tracking */
    int64_t now_us = esp_timer_get_time();

    /* Apply compensations in order */

    /* 1. Temperature self-heating correction */
    apply_temp_self_heat_correction(data);

    /* 2. Humidity passthrough (no correction) */
    apply_humidity_passthrough(data);

    /* 3. Pressure passthrough (used as reference) */
    apply_pressure_passthrough(data);

    /* 4. PM humidity correction */
    apply_pm_rh_correction(data);

    /* 5. CO2 pressure compensation */
    apply_co2_pressure_compensation(data);

    /* 6. CO2 ABC baseline correction */
    apply_co2_abc_correction(data, now_us);
}

esp_err_t fusion_reset_abc(void)
{
#ifdef CONFIG_FUSION_CO2_ABC_ENABLE
    ESP_LOGI(TAG, "Resetting CO2 ABC baseline");

    memset(&s_abc_state, 0, sizeof(abc_state_t));
    s_abc_state.baseline_ppm = 400;
    s_abc_state.daily_minimum = INFINITY;

    /* Clear from NVS */
    nvs_handle_t h;
    if (nvs_open(FUSION_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "abc_baseline");
        nvs_commit(h);
        nvs_close(h);
    }

    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t fusion_set_pm_rh_coeffs(float a, float b)
{
    if (a < 0.0f || a > 2.0f || b < 1.0f || b > 10.0f) {
        ESP_LOGE(TAG, "Invalid PM RH coefficients: a=%.3f, b=%.3f", a, b);
        return ESP_ERR_INVALID_ARG;
    }

    s_pm_rh_a = a;
    s_pm_rh_b = b;

    ESP_LOGI(TAG, "PM RH coefficients updated: a=%.3f, b=%.3f", a, b);

    /* Persist to NVS */
    nvs_handle_t h;
    if (nvs_open(FUSION_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        save_float_to_nvs(h, "pm_rh_a", a);
        save_float_to_nvs(h, "pm_rh_b", b);
        nvs_commit(h);
        nvs_close(h);
    }

    return ESP_OK;
}

esp_err_t fusion_set_temp_offset(float offset_c)
{
    if (fabs(offset_c) > 10.0f) {
        ESP_LOGE(TAG, "Temperature offset too large: %.2f C", offset_c);
        return ESP_ERR_INVALID_ARG;
    }

    s_temp_offset_c = offset_c;

    ESP_LOGI(TAG, "Temperature self-heating offset updated: %.2f C", offset_c);

    /* Persist to NVS */
    nvs_handle_t h;
    if (nvs_open(FUSION_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        save_float_to_nvs(h, "temp_offset", offset_c);
        nvs_commit(h);
        nvs_close(h);
    }

    return ESP_OK;
}

void fusion_get_status(float *pm_rh_a, float *pm_rh_b, float *temp_offset,
                       uint16_t *abc_baseline, uint8_t *abc_confidence)
{
    if (pm_rh_a) *pm_rh_a = s_pm_rh_a;
    if (pm_rh_b) *pm_rh_b = s_pm_rh_b;
    if (temp_offset) *temp_offset = s_temp_offset_c;
    if (abc_baseline) *abc_baseline = s_abc_state.baseline_ppm;
    if (abc_confidence) *abc_confidence = s_abc_state.confidence_pct;
}
