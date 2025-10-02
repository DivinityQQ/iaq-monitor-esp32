/* components/sensor_drivers/sensor_sim.c */
#include "sensor_sim.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <math.h>

#ifdef CONFIG_IAQ_SIMULATION

/* Add small random jitter to simulated values for realism */
static float add_jitter(float base, float range)
{
    float random_factor = (float)esp_random() / (float)UINT32_MAX;
    return base + (random_factor * 2.0f - 1.0f) * range;
}

esp_err_t sensor_sim_read_temperature(float *out_celsius)
{
    if (!out_celsius) return ESP_ERR_INVALID_ARG;

    /* Simulate indoor temperature: 20-24°C with small variations */
    *out_celsius = add_jitter(22.0f, 1.5f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_mcu_temperature(float *out_celsius)
{
    if (!out_celsius) return ESP_ERR_INVALID_ARG;

    /* MCU typically runs 5-10°C warmer than ambient */
    *out_celsius = add_jitter(30.0f, 2.0f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_humidity(float *out_rh)
{
    if (!out_rh) return ESP_ERR_INVALID_ARG;

    /* Simulate indoor humidity: 40-60%RH */
    *out_rh = add_jitter(50.0f, 8.0f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_pressure(float *out_hpa)
{
    if (!out_hpa) return ESP_ERR_INVALID_ARG;

    /* Simulate sea-level pressure: ~1013 hPa */
    *out_hpa = add_jitter(1013.25f, 5.0f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_co2(float *out_ppm)
{
    if (!out_ppm) return ESP_ERR_INVALID_ARG;

    /* Simulate diurnal CO2 pattern: 400-900 ppm */
    /* Use time-based variation to simulate occupancy patterns */
    int64_t now_us = esp_timer_get_time();
    int64_t seconds = now_us / 1000000LL;

    /* 10-minute cycle for demo purposes */
    float cycle = (float)(seconds % 600) / 600.0f;
    float base_co2 = 400.0f + (500.0f * cycle);

    *out_ppm = add_jitter(base_co2, 30.0f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_voc_nox(uint16_t *out_voc, uint16_t *out_nox)
{
    if (!out_voc || !out_nox) return ESP_ERR_INVALID_ARG;

    /* VOC index: 100-200 (normal indoor air) */
    *out_voc = (uint16_t)add_jitter(150.0f, 30.0f);

    /* NOx index: 100-150 (low pollution) */
    *out_nox = (uint16_t)add_jitter(125.0f, 20.0f);

    return ESP_OK;
}

esp_err_t sensor_sim_read_pm(float *out_pm1, float *out_pm25, float *out_pm10)
{
    if (!out_pm1 || !out_pm25 || !out_pm10) return ESP_ERR_INVALID_ARG;

    /* Simulate clean indoor air with occasional spikes */
    int64_t now_us = esp_timer_get_time();
    int64_t seconds = now_us / 1000000LL;

    /* Spike every 5 minutes for 30 seconds (e.g., cooking event) */
    bool spike = ((seconds % 300) < 30);

    float pm25_base = spike ? 35.0f : 8.0f;

    *out_pm1 = add_jitter(pm25_base * 0.7f, 1.0f);
    *out_pm25 = add_jitter(pm25_base, 2.0f);
    *out_pm10 = add_jitter(pm25_base * 1.3f, 3.0f);

    return ESP_OK;
}

#endif /* CONFIG_IAQ_SIMULATION */
