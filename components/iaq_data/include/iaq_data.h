/* components/iaq_data/include/iaq_data.h */
#ifndef IAQ_DATA_H
#define IAQ_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * Pressure trend classification based on 3-hour change.
 */
typedef enum {
    PRESSURE_TREND_RISING,      // > +threshold hPa/3hr
    PRESSURE_TREND_STABLE,      // within ±threshold
    PRESSURE_TREND_FALLING,     // < -threshold hPa/3hr
    PRESSURE_TREND_UNKNOWN      // insufficient data
} pressure_trend_t;

/**
 * Compensated (fused) sensor values after cross-sensor correction.
 * These are the "clean" values that should be displayed and published.
 */
typedef struct {
    float temp_c;               // Compensated temperature (self-heating offset applied)
    float rh_pct;               // Compensated relative humidity
    float pressure_pa;          // Pressure (raw, used as reference for other compensations)
    float pm1_ugm3;             // PM1.0 (raw, for diagnostics)
    float pm25_ugm3;            // PM2.5 (RH-corrected)
    float pm10_ugm3;            // PM10 (RH-corrected)
    float co2_ppm;              // CO2 (pressure-compensated + ABC)
} iaq_fused_data_t;

/**
 * Derived metrics calculated from fused sensor data.
 */
typedef struct {
    /* AQI (EPA) */
    uint16_t aqi_value;         // 0-500
    const char* aqi_category;   // "Good", "Moderate", "Unhealthy for Sensitive Groups", etc.
    const char* aqi_dominant;   // "pm25" or "pm10" (which pollutant drives AQI)
    float aqi_pm25_subindex;    // PM2.5 contribution to AQI
    float aqi_pm10_subindex;    // PM10 contribution to AQI

    /* Thermal comfort (Tier 1: added abs_humidity) */
    float dew_point_c;          // Dew point (Magnus formula)
    float abs_humidity_gm3;     // Absolute humidity (g/m³) - moisture content
    float heat_index_c;         // Heat index (NOAA formula)
    uint8_t comfort_score;      // 0-100 (100 = perfectly comfortable)
    const char* comfort_category; // "Comfortable", "Warm", "Cold", etc.

    /* Air quality scores (Tier 1: added VOC/NOx categories) */
    uint8_t co2_score;          // 0-100 (100 = excellent ventilation)
    const char* voc_category;   // "Excellent", "Good", "Moderate", "Poor", "Very Poor", "Severe"
    const char* nox_category;   // Same categories as VOC
    uint8_t overall_iaq_score;  // 0-100 composite score

    /* Mold risk (Tier 1) */
    uint8_t mold_risk_score;    // 0-100 (0=no risk, 100=high risk)
    const char* mold_risk_category; // "Low", "Moderate", "High", "Severe"

    /* Trends & rates (Tier 1: added CO2 rate, PM spike) */
    pressure_trend_t pressure_trend;
    float pressure_delta_3hr_hpa; // Actual change over 3 hours
    float co2_rate_ppm_hr;      // Rate of change (ppm/hour) - for occupancy/ventilation detection
    bool pm25_spike_detected;   // Sudden PM2.5 increase (cooking, smoking, outdoor pollution)
} iaq_metrics_t;

/**
 * Diagnostics for sensor fusion algorithms.
 * Published to optional diagnostics topic for validation/tuning.
 */
typedef struct {
    float pm_rh_factor;         // Applied PM RH correction factor (1.0 = no correction)
    float co2_pressure_offset_ppm; // Applied CO2 pressure compensation (ppm)
    float temp_self_heat_offset_c; // Applied temperature self-heating offset
    uint16_t co2_abc_baseline_ppm; // Current CO2 ABC baseline
    uint8_t co2_abc_confidence_pct; // ABC confidence (0-100, based on # nights tracked)
    uint8_t pm25_quality;       // PM2.5 quality score (0-100, based on RH conditions)
    float pm1_pm25_ratio;       // PM1/PM2.5 ratio for sensor health check (should be 0.6-0.9)
} iaq_fusion_diagnostics_t;

/**
 * Global data structure for all IAQ measurements and system state.
 * Access must be protected by mutex.
 */
typedef struct {
    SemaphoreHandle_t mutex;

    /* LEGACY FIELDS - Kept for backward compatibility, will be removed in v1.0.0
     * - New code should use fused.* for compensated values and metrics.* for derived data
     * - These fields may not be populated in future releases */

    /* RAW sensor readings (uncompensated) - legacy fields */
    float temperature;          // °C (SHT41 raw)
    float mcu_temperature;      // °C (MCU internal sensor)
    float humidity;             // %RH (SHT41 raw)
    float pressure;             // hPa (BMP280 raw)
    float pressure_trend;       // DEPRECATED: use metrics.pressure_delta_3hr_hpa

    float co2_ppm;              // CO2 raw (ppm)
    float pm1_0;                // PM1.0 raw (µg/m³)
    float pm2_5;                // PM2.5 raw (µg/m³)
    float pm10;                 // PM10 raw (µg/m³)
    uint16_t voc_index;         // VOC index (0-500)
    uint16_t nox_index;         // NOx index (0-500)

    /* DEPRECATED derived metrics - use metrics.* instead */
    uint16_t aqi;               // DEPRECATED: use metrics.aqi_value
    const char *comfort;        // DEPRECATED: use metrics.comfort_category

    /* FUSED (compensated) sensor values - use these for display/publishing */
    iaq_fused_data_t fused;

    /* DERIVED metrics (AQI, comfort, trends) */
    iaq_metrics_t metrics;

    /* FUSION diagnostics (for validation/tuning) */
    iaq_fusion_diagnostics_t fusion_diag;

    /* Metadata */
    struct {
        /* Per-sensor last update timestamps (microseconds since boot). 0 = never */
        int64_t mcu;
        int64_t sht41;
        int64_t bmp280;
        int64_t sgp41;
        int64_t pms5003;
        int64_t s8;
    } updated_at;

    /* Validity flags - true if sensor has provided at least one valid reading */
    struct {
        bool temperature;       // SHT41 temperature valid
        bool mcu_temperature;   // MCU internal temperature valid
        bool humidity;          // SHT41 humidity valid
        bool pressure;          // BMP280 pressure valid
        bool co2_ppm;           // S8 CO2 valid
        bool pm1_0;             // PMS5003 PM1.0 valid
        bool pm2_5;             // PMS5003 PM2.5 valid
        bool pm10;              // PMS5003 PM10 valid
        bool voc_index;         // SGP41 VOC index valid
        bool nox_index;         // SGP41 NOx index valid
    } valid;

    uint8_t overall_quality;    // Overall air quality (0-100)

    /* System status */
    struct {
        bool wifi_connected;
        bool mqtt_connected;
        int32_t wifi_rssi;
        uint32_t uptime_seconds;
        uint32_t free_heap;
        uint32_t min_free_heap;
    } system;

} iaq_data_t;

/**
 * Initialize the global data structure.
 * Must be called before any other iaq_data functions.
 *
 * @return ESP_OK on success, ESP_FAIL if mutex creation fails
 */
esp_err_t iaq_data_init(void);

/**
 * Get a pointer to the global data structure.
 * WARNING: You must call iaq_data_lock() before reading/writing
 * and iaq_data_unlock() when done.
 *
 * @return Pointer to global data
 */
iaq_data_t* iaq_data_get(void);

/**
 * Lock the data structure for exclusive access.
 *
 * @param timeout_ms Timeout in milliseconds (portMAX_DELAY for infinite)
 * @return true if lock acquired, false on timeout
 */
bool iaq_data_lock(uint32_t timeout_ms);

/**
 * Unlock the data structure.
 */
void iaq_data_unlock(void);

/**
 * Scoped lock helper that guarantees unlock even on early returns.
 * Usage:
 *   IAQ_DATA_WITH_LOCK() {
 *       iaq_data_t *data = iaq_data_get();
 *       // ... read/write data
 *   }
 */
#define IAQ_DATA_WITH_LOCK() \
    for (int _iaq_lock_once = iaq_data_lock(portMAX_DELAY); _iaq_lock_once; _iaq_lock_once = (iaq_data_unlock(), 0))

#endif /* IAQ_DATA_H */
