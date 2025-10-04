/* components/sensor_coordinator/metrics_calc.c */
#include "metrics_calc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "iaq_config.h"

static const char *TAG = "METRICS";

/* Metrics timer fires every 5 s; downsample certain histories to control memory use. */
#define METRICS_SAMPLE_PERIOD_SEC 5U
#define PRESSURE_SAMPLE_INTERVAL_SEC 150U
#define CO2_SAMPLE_INTERVAL_SEC 60U
#define PM_SAMPLE_INTERVAL_SEC 30U

/* ===== Pressure Trend Tracking ===== */
#ifdef CONFIG_METRICS_PRESSURE_TREND_ENABLE
#define PRESSURE_HISTORY_SIZE 144  /* 6 hours @ 2.5-minute intervals */

static struct {
    float pressure_pa[PRESSURE_HISTORY_SIZE];
    int64_t timestamps_us[PRESSURE_HISTORY_SIZE];
    uint8_t head;
    uint8_t count;
} s_pressure_history = {0};
static uint32_t s_pressure_sample_elapsed_sec = PRESSURE_SAMPLE_INTERVAL_SEC;
#endif

/* ===== CO2 Rate of Change Tracking ===== */
#ifdef CONFIG_METRICS_CO2_RATE_ENABLE
#define CO2_HISTORY_SIZE 64  /* ~1 hour of history at 60-second sampling */

static struct {
    float co2_ppm[CO2_HISTORY_SIZE];
    int64_t timestamps_us[CO2_HISTORY_SIZE];
    uint8_t head;
    uint8_t count;
} s_co2_history = {0};
static uint32_t s_co2_sample_elapsed_sec = CO2_SAMPLE_INTERVAL_SEC;
#endif

/* ===== PM2.5 Spike Detection ===== */
#ifdef CONFIG_METRICS_PM_SPIKE_DETECTION_ENABLE
#define PM_HISTORY_SIZE 120  /* 60 minutes @ 30-second intervals */

static struct {
    float pm25_ugm3[PM_HISTORY_SIZE];
    int64_t timestamps_us[PM_HISTORY_SIZE];
    uint8_t head;
    uint8_t count;
} s_pm_history = {0};
static uint32_t s_pm_sample_elapsed_sec = PM_SAMPLE_INTERVAL_SEC;
#endif

esp_err_t metrics_init(void)
{
    ESP_LOGI(TAG, "Initializing metrics calculation");

#ifdef CONFIG_METRICS_PRESSURE_TREND_ENABLE
    memset(&s_pressure_history, 0, sizeof(s_pressure_history));
#endif

#ifdef CONFIG_METRICS_CO2_RATE_ENABLE
    memset(&s_co2_history, 0, sizeof(s_co2_history));
#endif

#ifdef CONFIG_METRICS_PM_SPIKE_DETECTION_ENABLE
    memset(&s_pm_history, 0, sizeof(s_pm_history));
#endif

    ESP_LOGI(TAG, "Metrics calculation initialized");
    return ESP_OK;
}

/* ========== EPA AQI Calculation ========== */

#ifdef CONFIG_METRICS_AQI_ENABLE

typedef struct {
    float c_lo, c_hi;    /* Concentration breakpoints (µg/m³) */
    uint16_t i_lo, i_hi; /* Index breakpoints */
} aqi_breakpoint_t;

/* EPA PM2.5 breakpoints (24-hour average, but we use instantaneous) */
static const aqi_breakpoint_t pm25_breakpoints[] = {
    {0.0,   12.0,   0,   50},   /* Good */
    {12.1,  35.4,   51,  100},  /* Moderate */
    {35.5,  55.4,   101, 150},  /* Unhealthy for Sensitive */
    {55.5,  150.4,  151, 200},  /* Unhealthy */
    {150.5, 250.4,  201, 300},  /* Very Unhealthy */
    {250.5, 500.0,  301, 500}   /* Hazardous */
};

/* EPA PM10 breakpoints (24-hour average) */
static const aqi_breakpoint_t pm10_breakpoints[] = {
    {0,   54,   0,   50},
    {55,  154,  51,  100},
    {155, 254,  101, 150},
    {255, 354,  151, 200},
    {355, 424,  201, 300},
    {425, 604,  301, 500}
};

#define PM25_BREAKPOINT_COUNT (sizeof(pm25_breakpoints) / sizeof(aqi_breakpoint_t))
#define PM10_BREAKPOINT_COUNT (sizeof(pm10_breakpoints) / sizeof(aqi_breakpoint_t))

/**
 * Calculate AQI sub-index using piecewise linear interpolation.
 * Formula: I = ((I_hi - I_lo) / (C_hi - C_lo)) * (C - C_lo) + I_lo
 */
static uint16_t calculate_aqi_subindex(float concentration, const aqi_breakpoint_t *breakpoints, size_t count)
{
    if (isnan(concentration) || concentration < 0.0f) {
        return UINT16_MAX;  /* Invalid */
    }

    /* Find applicable breakpoint */
    for (size_t i = 0; i < count; i++) {
        if (concentration >= breakpoints[i].c_lo && concentration <= breakpoints[i].c_hi) {
            float c = concentration;
            float c_lo = breakpoints[i].c_lo;
            float c_hi = breakpoints[i].c_hi;
            float i_lo = (float)breakpoints[i].i_lo;
            float i_hi = (float)breakpoints[i].i_hi;

            float index = ((i_hi - i_lo) / (c_hi - c_lo)) * (c - c_lo) + i_lo;
            return (uint16_t)roundf(index);
        }
    }

    /* Above highest breakpoint */
    if (concentration > breakpoints[count - 1].c_hi) {
        return 500;  /* Hazardous max */
    }

    return 0;  /* Below lowest breakpoint */
}

/**
 * Map AQI value to category string.
 */
static const char* aqi_value_to_category(uint16_t aqi)
{
    if (aqi <= 50)       return "Good";
    if (aqi <= 100)      return "Moderate";
    if (aqi <= 150)      return "Unhealthy for Sensitive";
    if (aqi <= 200)      return "Unhealthy";
    if (aqi <= 300)      return "Very Unhealthy";
    return "Hazardous";
}

/**
 * Calculate EPA AQI from PM2.5 and PM10.
 */
static void calculate_aqi(iaq_data_t *data)
{
    if (!data->valid.pm2_5 && !data->valid.pm10) {
        data->metrics.aqi_value = UINT16_MAX;
        data->metrics.aqi_category = "unknown";
        data->metrics.aqi_dominant = "none";
        data->metrics.aqi_pm25_subindex = NAN;
        data->metrics.aqi_pm10_subindex = NAN;
        return;
    }

    uint16_t pm25_aqi = UINT16_MAX;
    uint16_t pm10_aqi = UINT16_MAX;

    if (data->valid.pm2_5) {
        pm25_aqi = calculate_aqi_subindex(data->fused.pm25_ugm3, pm25_breakpoints, PM25_BREAKPOINT_COUNT);
        data->metrics.aqi_pm25_subindex = (float)pm25_aqi;
    } else {
        data->metrics.aqi_pm25_subindex = NAN;
    }

    if (data->valid.pm10) {
        pm10_aqi = calculate_aqi_subindex(data->fused.pm10_ugm3, pm10_breakpoints, PM10_BREAKPOINT_COUNT);
        data->metrics.aqi_pm10_subindex = (float)pm10_aqi;
    } else {
        data->metrics.aqi_pm10_subindex = NAN;
    }

    /* Overall AQI is the maximum of PM2.5 and PM10 sub-indices */
    uint16_t overall_aqi = 0;
    const char *dominant = "none";

    if (pm25_aqi != UINT16_MAX && pm10_aqi != UINT16_MAX) {
        if (pm25_aqi >= pm10_aqi) {
            overall_aqi = pm25_aqi;
            dominant = "pm25";
        } else {
            overall_aqi = pm10_aqi;
            dominant = "pm10";
        }
    } else if (pm25_aqi != UINT16_MAX) {
        overall_aqi = pm25_aqi;
        dominant = "pm25";
    } else if (pm10_aqi != UINT16_MAX) {
        overall_aqi = pm10_aqi;
        dominant = "pm10";
    }

    data->metrics.aqi_value = overall_aqi;
    data->metrics.aqi_category = aqi_value_to_category(overall_aqi);
    data->metrics.aqi_dominant = dominant;
}
#endif /* CONFIG_METRICS_AQI_ENABLE */

/* ========== Thermal Comfort Calculations ========== */

#ifdef CONFIG_METRICS_COMFORT_ENABLE

/**
 * Calculate dew point using Magnus formula.
 * Formula: Td = (b * gamma) / (a - gamma)
 *   where gamma = (a * T / (b + T)) + ln(RH / 100)
 *   a = 17.62, b = 243.12°C
 */
static float calculate_dew_point(float temp_c, float rh_pct)
{
    if (isnan(temp_c) || isnan(rh_pct) || rh_pct <= 0.0f) {
        return NAN;
    }

    const float a = 17.62f;
    const float b = 243.12f;

    float gamma = (a * temp_c / (b + temp_c)) + logf(rh_pct / 100.0f);
    float dew_point = (b * gamma) / (a - gamma);

    return dew_point;
}

/**
 * Calculate absolute humidity (g/m³).
 * Formula: AH = (6.112 * exp((17.67 * T) / (T + 243.5)) * RH * 2.1674) / (273.15 + T)
 */
static float calculate_absolute_humidity(float temp_c, float rh_pct)
{
    if (isnan(temp_c) || isnan(rh_pct)) {
        return NAN;
    }

    float temp_k = temp_c + 273.15f;
    float es = 6.112f * expf((17.67f * temp_c) / (temp_c + 243.5f));  /* Saturation vapor pressure (hPa) */
    float e = es * (rh_pct / 100.0f);  /* Actual vapor pressure (hPa) */
    float ah = (e * 2.1674f) / temp_k;  /* g/m³ (constant 2.1674 = 1000/461.5 where 461.5 is water vapor gas constant) */

    return ah;
}

/**
 * Calculate heat index using simplified NOAA formula.
 * Only applies above 27°C (80°F).
 */
static float calculate_heat_index(float temp_c, float rh_pct)
{
    if (isnan(temp_c) || isnan(rh_pct)) {
        return NAN;
    }

    /* Heat index only relevant above 27°C */
    if (temp_c < 27.0f) {
        return temp_c;
    }

    /* Simplified Rothfusz regression (in Fahrenheit, then convert) */
    float T = temp_c * 9.0f / 5.0f + 32.0f;  /* °C -> °F */
    float R = rh_pct;

    float HI = -42.379f
               + 2.04901523f * T
               + 10.14333127f * R
               - 0.22475541f * T * R
               - 0.00683783f * T * T
               - 0.05481717f * R * R
               + 0.00122874f * T * T * R
               + 0.00085282f * T * R * R
               - 0.00000199f * T * T * R * R;

    float hi_c = (HI - 32.0f) * 5.0f / 9.0f;  /* °F -> °C */
    return hi_c;
}

/**
 * Calculate comfort score (0-100) and category.
 * Penalties for deviation from target temperature and humidity.
 */
static void calculate_comfort_score(iaq_data_t *data)
{
    if (!data->valid.temperature || !data->valid.humidity) {
        data->metrics.comfort_score = 0;
        data->metrics.comfort_category = "unknown";
        data->metrics.dew_point_c = NAN;
        data->metrics.abs_humidity_gm3 = NAN;
        data->metrics.heat_index_c = NAN;
        return;
    }

    float temp = data->fused.temp_c;
    float rh = data->fused.rh_pct;

    /* Calculate derived metrics */
    float dew_point = calculate_dew_point(temp, rh);
    float abs_humidity = calculate_absolute_humidity(temp, rh);
    float heat_index = calculate_heat_index(temp, rh);

    data->metrics.dew_point_c = dew_point;
    data->metrics.abs_humidity_gm3 = abs_humidity;
    data->metrics.heat_index_c = heat_index;

    /* Comfort scoring (start from 100, subtract penalties) */
    int score = 100;

    /* Temperature deviation penalty */
    float target_temp = (float)CONFIG_METRICS_COMFORT_TARGET_TEMP_C;
    float temp_dev = fabsf(temp - target_temp);
    if (temp_dev > 0.0f) {
        score -= (int)(temp_dev * 5.0f);  /* -5 points per °C deviation */
    }

    /* Humidity deviation penalty */
    float target_rh = (float)CONFIG_METRICS_COMFORT_TARGET_RH_PCT;
    float rh_dev = fabsf(rh - target_rh);
    if (rh_dev > 0.0f) {
        score -= (int)(rh_dev * 0.5f);  /* -0.5 points per %RH deviation */
    }

    /* Dew point penalty (> 18°C feels muggy) */
    if (!isnan(dew_point) && dew_point > 18.0f) {
        score -= 10;
    }

    /* Heat index penalty (> 27°C feels hot) */
    if (!isnan(heat_index) && heat_index > 27.0f) {
        score -= (int)((heat_index - 27.0f) * 3.0f);  /* -3 points per °C above 27 */
    }

    /* Clamp to 0-100 */
    if (score < 0) score = 0;
    if (score > 100) score = 100;

    data->metrics.comfort_score = (uint8_t)score;

    /* Map to category */
    if (score >= 80) {
        data->metrics.comfort_category = "Comfortable";
    } else if (score >= 60) {
        data->metrics.comfort_category = "Slightly Uncomfortable";
    } else if (score >= 40) {
        data->metrics.comfort_category = "Uncomfortable";
    } else if (score >= 20) {
        data->metrics.comfort_category = "Very Uncomfortable";
    } else {
        data->metrics.comfort_category = "Extremely Uncomfortable";
    }
}
#endif /* CONFIG_METRICS_COMFORT_ENABLE */

/* ========== Air Quality Scores ========== */

/**
 * Calculate CO2 score (0-100, higher is better).
 * Piecewise linear mapping:
 *   400-800 ppm   -> 100-80
 *   800-1000 ppm  -> 80-60
 *   1000-1400 ppm -> 60-30
 *   1400-2000 ppm -> 30-10
 *   >2000 ppm     -> 10-0
 */
static void calculate_co2_score(iaq_data_t *data)
{
    if (!data->valid.co2_ppm) {
        data->metrics.co2_score = 0;
        return;
    }

    float co2 = data->fused.co2_ppm;
    uint8_t score = 0;

    if (co2 <= 800.0f) {
        score = 100 - (uint8_t)((co2 - 400.0f) / 400.0f * 20.0f);  /* 100 -> 80 */
    } else if (co2 <= 1000.0f) {
        score = 80 - (uint8_t)((co2 - 800.0f) / 200.0f * 20.0f);  /* 80 -> 60 */
    } else if (co2 <= 1400.0f) {
        score = 60 - (uint8_t)((co2 - 1000.0f) / 400.0f * 30.0f);  /* 60 -> 30 */
    } else if (co2 <= 2000.0f) {
        score = 30 - (uint8_t)((co2 - 1400.0f) / 600.0f * 20.0f);  /* 30 -> 10 */
    } else {
        score = 10 - (uint8_t)((co2 - 2000.0f) / 1000.0f * 10.0f);  /* 10 -> 0 */
        if (score > 10) score = 0;  /* Clamp at 0 */
    }

    data->metrics.co2_score = score;
}

/**
 * Calculate overall IAQ score (0-100) as weighted average.
 * Weights: AQI (40%), CO2 (40%), Comfort (20%)
 */
static void calculate_overall_iaq_score(iaq_data_t *data)
{
    /* Normalize AQI (0-500) to 0-100 scale (inverted: lower AQI = higher score) */
    float aqi_normalized = 0.0f;
    if (data->metrics.aqi_value != UINT16_MAX && data->metrics.aqi_value <= 500) {
        aqi_normalized = 100.0f - (data->metrics.aqi_value / 500.0f * 100.0f);
        if (aqi_normalized < 0.0f) aqi_normalized = 0.0f;
    }

    float co2_score = (float)data->metrics.co2_score;
    float comfort_score = (float)data->metrics.comfort_score;

    /* Weighted average */
    float overall = 0.4f * aqi_normalized + 0.4f * co2_score + 0.2f * comfort_score;

    data->metrics.overall_iaq_score = (uint8_t)roundf(overall);
}

/* ========== VOC/NOx Categories ========== */

#ifdef CONFIG_METRICS_VOC_NOX_CATEGORIES_ENABLE

/**
 * Map SGP41 VOC/NOx index (0-500) to user-friendly category.
 */
static const char* index_to_category(uint16_t index)
{
    if (index == UINT16_MAX) return "unknown";
    if (index <= 100)  return "Excellent";
    if (index <= 150)  return "Good";
    if (index <= 200)  return "Moderate";
    if (index <= 300)  return "Poor";
    if (index <= 400)  return "Very Poor";
    return "Severe";
}

static void calculate_voc_nox_categories(iaq_data_t *data)
{
    if (data->valid.voc_index) {
        data->metrics.voc_category = index_to_category(data->voc_index);
    } else {
        data->metrics.voc_category = "unknown";
    }

    if (data->valid.nox_index) {
        data->metrics.nox_category = index_to_category(data->nox_index);
    } else {
        data->metrics.nox_category = "unknown";
    }
}
#endif /* CONFIG_METRICS_VOC_NOX_CATEGORIES_ENABLE */

/* ========== Mold Risk Index ========== */

#ifdef CONFIG_METRICS_MOLD_RISK_ENABLE

/**
 * Calculate mold risk score (0-100) based on:
 * - RH > 65% (threshold for mold growth)
 * - Dew point proximity to cold surfaces
 * - Duration (future enhancement: track sustained high RH)
 */
static void calculate_mold_risk(iaq_data_t *data)
{
    if (!data->valid.temperature || !data->valid.humidity) {
        data->metrics.mold_risk_score = 0;
        data->metrics.mold_risk_category = "unknown";
        return;
    }

    float rh = data->fused.rh_pct;
    float dew_point = data->metrics.dew_point_c;
    float temp = data->fused.temp_c;

    int risk_score = 0;

    /* Base risk from RH */
    if (rh >= 65.0f) {
        risk_score += (int)((rh - 65.0f) * 2.0f);  /* +2 points per % above 65 */
    }

    /* Risk from dew point proximity to cold surfaces */
    if (!isnan(dew_point)) {
        float cold_surface_temp = temp - (float)CONFIG_METRICS_MOLD_RISK_COLD_SURFACE_OFFSET_C;
        float dew_margin = cold_surface_temp - dew_point;

        if (dew_margin < 0.0f) {
            /* Condensation likely! */
            risk_score += 50;
        } else if (dew_margin < 3.0f) {
            /* Close to condensation */
            risk_score += (int)((3.0f - dew_margin) / 3.0f * 30.0f);  /* +30 points max */
        }
    }

    /* Clamp to 0-100 */
    if (risk_score < 0) risk_score = 0;
    if (risk_score > 100) risk_score = 100;

    data->metrics.mold_risk_score = (uint8_t)risk_score;

    /* Map to category */
    if (risk_score < 25) {
        data->metrics.mold_risk_category = "Low";
    } else if (risk_score < 50) {
        data->metrics.mold_risk_category = "Moderate";
    } else if (risk_score < 75) {
        data->metrics.mold_risk_category = "High";
    } else {
        data->metrics.mold_risk_category = "Severe";
    }
}
#endif /* CONFIG_METRICS_MOLD_RISK_ENABLE */

/* ========== Pressure Trend ========== */

#ifdef CONFIG_METRICS_PRESSURE_TREND_ENABLE

static void update_pressure_trend(iaq_data_t *data)
{
    if (!data->valid.pressure) {
        data->metrics.pressure_trend = PRESSURE_TREND_UNKNOWN;
        data->metrics.pressure_delta_3hr_hpa = NAN;
        return;
    }

    /* Only record to the 3-hour pressure buffer at ~150 s cadence. */
    s_pressure_sample_elapsed_sec += METRICS_SAMPLE_PERIOD_SEC;
    if (s_pressure_sample_elapsed_sec >= PRESSURE_SAMPLE_INTERVAL_SEC) {
        s_pressure_sample_elapsed_sec = 0;

        float pressure_pa = data->fused.pressure_pa;
        int64_t now_us = esp_timer_get_time();

        s_pressure_history.pressure_pa[s_pressure_history.head] = pressure_pa;
        s_pressure_history.timestamps_us[s_pressure_history.head] = now_us;
        s_pressure_history.head = (s_pressure_history.head + 1) % PRESSURE_HISTORY_SIZE;
        if (s_pressure_history.count < PRESSURE_HISTORY_SIZE) {
            s_pressure_history.count++;
        }
    }

    /* Need at least 2 samples */
    if (s_pressure_history.count < 2) {
        data->metrics.pressure_trend = PRESSURE_TREND_UNKNOWN;
        data->metrics.pressure_delta_3hr_hpa = NAN;
        return;
    }

    /* Get oldest and latest */
    uint8_t oldest_idx = (s_pressure_history.head + PRESSURE_HISTORY_SIZE - s_pressure_history.count) % PRESSURE_HISTORY_SIZE;
    uint8_t latest_idx = (s_pressure_history.head + PRESSURE_HISTORY_SIZE - 1) % PRESSURE_HISTORY_SIZE;

    float p_oldest = s_pressure_history.pressure_pa[oldest_idx];
    float p_latest = s_pressure_history.pressure_pa[latest_idx];
    int64_t t_oldest = s_pressure_history.timestamps_us[oldest_idx];
    int64_t t_latest = s_pressure_history.timestamps_us[latest_idx];

    /* Calculate time span in hours */
    float span_hours = (float)(t_latest - t_oldest) / (3600.0f * 1000000.0f);

    /* Require at least 1 hour */
    if (span_hours < 1.0f) {
        data->metrics.pressure_trend = PRESSURE_TREND_UNKNOWN;
        data->metrics.pressure_delta_3hr_hpa = NAN;
        return;
    }

    /* Calculate change (Pa -> hPa) */
    float delta_hpa = (p_latest - p_oldest) / 100.0f;

    /* Normalize to window */
    float window_hours = (float)CONFIG_METRICS_PRESSURE_TREND_WINDOW_HR;
    float delta_normalized = delta_hpa * (window_hours / span_hours);

    data->metrics.pressure_delta_3hr_hpa = delta_normalized;

    /* Apply threshold */
    float threshold = atof(CONFIG_METRICS_PRESSURE_TREND_THRESHOLD_HPA);
    if (delta_normalized > threshold) {
        data->metrics.pressure_trend = PRESSURE_TREND_RISING;
    } else if (delta_normalized < -threshold) {
        data->metrics.pressure_trend = PRESSURE_TREND_FALLING;
    } else {
        data->metrics.pressure_trend = PRESSURE_TREND_STABLE;
    }
}
#endif /* CONFIG_METRICS_PRESSURE_TREND_ENABLE */

/* ========== CO2 Rate of Change ========== */

#ifdef CONFIG_METRICS_CO2_RATE_ENABLE

static void update_co2_rate(iaq_data_t *data)
{
    if (!data->valid.co2_ppm) {
        data->metrics.co2_rate_ppm_hr = NAN;
        return;
    }

    /* Record CO2 history roughly once per minute for trend calculations. */
    s_co2_sample_elapsed_sec += METRICS_SAMPLE_PERIOD_SEC;
    if (s_co2_sample_elapsed_sec >= CO2_SAMPLE_INTERVAL_SEC) {
        s_co2_sample_elapsed_sec = 0;

        float co2 = data->fused.co2_ppm;
        int64_t now_us = esp_timer_get_time();

        s_co2_history.co2_ppm[s_co2_history.head] = co2;
        s_co2_history.timestamps_us[s_co2_history.head] = now_us;
        s_co2_history.head = (s_co2_history.head + 1) % CO2_HISTORY_SIZE;
        if (s_co2_history.count < CO2_HISTORY_SIZE) {
            s_co2_history.count++;
        }
    }

    if (s_co2_history.count < 2) {
        data->metrics.co2_rate_ppm_hr = NAN;
        return;
    }

    uint32_t window_minutes = CONFIG_METRICS_CO2_RATE_WINDOW_MIN;
    int64_t window_us = (int64_t)window_minutes * 60 * 1000000;

    int oldest_in_window = -1;
    int64_t now_us = esp_timer_get_time();
    for (int i = s_co2_history.count - 1; i >= 0; i--) {
        int idx = (s_co2_history.head + CO2_HISTORY_SIZE - 1 - i) % CO2_HISTORY_SIZE;
        if ((now_us - s_co2_history.timestamps_us[idx]) <= window_us) {
            oldest_in_window = idx;
        } else {
            break;
        }
    }

    if (oldest_in_window == -1) {
        data->metrics.co2_rate_ppm_hr = NAN;
        return;
    }

    uint8_t latest_idx = (s_co2_history.head + CO2_HISTORY_SIZE - 1) % CO2_HISTORY_SIZE;
    float co2_delta = s_co2_history.co2_ppm[latest_idx] - s_co2_history.co2_ppm[oldest_in_window];
    float time_delta_hr = (float)(s_co2_history.timestamps_us[latest_idx] - s_co2_history.timestamps_us[oldest_in_window]) / (3600.0f * 1000000.0f);

    if (time_delta_hr > 0.0f) {
        data->metrics.co2_rate_ppm_hr = co2_delta / time_delta_hr;
    } else {
        data->metrics.co2_rate_ppm_hr = NAN;
    }
}
#endif /* CONFIG_METRICS_CO2_RATE_ENABLE */

/* ========== PM2.5 Spike Detection ========== */

#ifdef CONFIG_METRICS_PM_SPIKE_DETECTION_ENABLE

static void update_pm_spike_detection(iaq_data_t *data)
{
    if (!data->valid.pm2_5) {
        data->metrics.pm25_spike_detected = false;
        return;
    }

    float pm25 = data->fused.pm25_ugm3;
    int64_t now_us = esp_timer_get_time();

    /* Track PM spikes at ~30 s cadence to reduce noise without missing events. */
    s_pm_sample_elapsed_sec += METRICS_SAMPLE_PERIOD_SEC;
    if (s_pm_sample_elapsed_sec >= PM_SAMPLE_INTERVAL_SEC) {
        s_pm_sample_elapsed_sec = 0;
        s_pm_history.pm25_ugm3[s_pm_history.head] = pm25;
        s_pm_history.timestamps_us[s_pm_history.head] = now_us;
        s_pm_history.head = (s_pm_history.head + 1) % PM_HISTORY_SIZE;
        if (s_pm_history.count < PM_HISTORY_SIZE) {
            s_pm_history.count++;
        }
    }

    if (s_pm_history.count < 5) {
        data->metrics.pm25_spike_detected = false;
        return;
    }

    uint32_t window_minutes = CONFIG_METRICS_PM_SPIKE_BASELINE_WINDOW_MIN;
    int64_t window_us = (int64_t)window_minutes * 60 * 1000000;

    float baseline_sum = 0.0f;
    int baseline_count = 0;

    for (int i = 1; i < s_pm_history.count; i++) {  /* Start from 1 to exclude current */
        int idx = (s_pm_history.head + PM_HISTORY_SIZE - 1 - i) % PM_HISTORY_SIZE;
        if ((now_us - s_pm_history.timestamps_us[idx]) <= window_us) {
            baseline_sum += s_pm_history.pm25_ugm3[idx];
            baseline_count++;
        } else {
            break;
        }
    }

    if (baseline_count == 0) {
        data->metrics.pm25_spike_detected = false;
        return;
    }

    float baseline = baseline_sum / (float)baseline_count;
    float spike_threshold = (float)CONFIG_METRICS_PM_SPIKE_THRESHOLD_UGPM3;

    data->metrics.pm25_spike_detected = ((pm25 - baseline) >= spike_threshold);
}
#endif /* CONFIG_METRICS_PM_SPIKE_DETECTION_ENABLE */

/* ========== Main Calculation Entry Point ========== */

void metrics_calculate_all(iaq_data_t *data)
{
    if (!data) {
        return;
    }

    /* Calculate AQI */
#ifdef CONFIG_METRICS_AQI_ENABLE
    calculate_aqi(data);
#endif

    /* Calculate thermal comfort */
#ifdef CONFIG_METRICS_COMFORT_ENABLE
    calculate_comfort_score(data);
#endif

    /* Calculate CO2 score */
    calculate_co2_score(data);

    /* Calculate overall IAQ score */
    calculate_overall_iaq_score(data);

    /* Calculate VOC/NOx categories */
#ifdef CONFIG_METRICS_VOC_NOX_CATEGORIES_ENABLE
    calculate_voc_nox_categories(data);
#endif

    /* Calculate mold risk */
#ifdef CONFIG_METRICS_MOLD_RISK_ENABLE
    calculate_mold_risk(data);
#endif

    /* Update pressure trend */
#ifdef CONFIG_METRICS_PRESSURE_TREND_ENABLE
    update_pressure_trend(data);
#endif

    /* Update CO2 rate */
#ifdef CONFIG_METRICS_CO2_RATE_ENABLE
    update_co2_rate(data);
#endif

    /* Update PM spike detection */
#ifdef CONFIG_METRICS_PM_SPIKE_DETECTION_ENABLE
    update_pm_spike_detection(data);
#endif
}
