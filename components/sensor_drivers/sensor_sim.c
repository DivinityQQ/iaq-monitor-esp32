/* components/sensor_drivers/sensor_sim.c */
#include "sensor_sim.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <math.h>

#ifdef CONFIG_IAQ_SIMULATION

/* Accelerated time: 20x real-time (1 real hour = 3 sim minutes, full day in 72 minutes) */
#define SIM_TIME_SCALE 20

/* Add small random jitter to simulated values for realism */
static float add_jitter(float base, float range)
{
    float random_factor = (float)esp_random() / (float)UINT32_MAX;
    return base + (random_factor * 2.0f - 1.0f) * range;
}

/* Get simulated time-of-day in seconds (0-86400, compressed to 4320 real seconds = 72 min) */
static int64_t get_sim_time_of_day(void)
{
    int64_t now_us = esp_timer_get_time();
    int64_t real_seconds = now_us / 1000000LL;
    int64_t sim_seconds = (real_seconds * SIM_TIME_SCALE) % 86400;
    return sim_seconds;
}

/* Simulated occupancy pattern (affects CO2, temp, humidity) */
static float get_occupancy_factor(int64_t sim_tod)
{
    /* Night (0-7am): 0.2 (sleeping, low activity)
     * Morning (7-9am): 0.8 (waking up, breakfast)
     * Day (9am-5pm): 0.3 (at work/school)
     * Evening (5-11pm): 1.0 (home, active, cooking)
     * Late night (11pm-midnight): 0.5 (winding down)
     */
    int hour = sim_tod / 3600;
    if (hour < 7) return 0.2f;                    // Night
    else if (hour < 9) return 0.8f;                // Morning
    else if (hour < 17) return 0.3f;               // Day
    else if (hour < 23) return 1.0f;               // Evening
    else return 0.5f;                              // Late night
}

esp_err_t sensor_sim_read_temperature(float *out_celsius)
{
    if (!out_celsius) return ESP_ERR_INVALID_ARG;

    int64_t sim_tod = get_sim_time_of_day();
    float occupancy = get_occupancy_factor(sim_tod);

    /* Base temp 20°C, rises 2-3°C with occupancy/activity */
    float base_temp = 20.0f + (occupancy * 2.5f);

    /* Add diurnal cycle (warmer in afternoon) */
    float hour_factor = sinf(((float)sim_tod / 86400.0f) * 2.0f * M_PI - M_PI/2.0f);
    base_temp += hour_factor * 1.5f;  // ±1.5°C variation

    *out_celsius = add_jitter(base_temp, 0.3f);
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

    int64_t sim_tod = get_sim_time_of_day();
    float occupancy = get_occupancy_factor(sim_tod);

    /* Base 45%RH, rises 10-15% with occupancy (breathing, cooking) */
    float base_rh = 45.0f + (occupancy * 10.0f);

    *out_rh = add_jitter(base_rh, 3.0f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_pressure(float *out_hpa)
{
    if (!out_hpa) return ESP_ERR_INVALID_ARG;

    /* Simulate weather system passing through (3hr changes) */
    int64_t sim_tod = get_sim_time_of_day();
    float hour = (float)sim_tod / 3600.0f;

    /* Sinusoidal pressure change: 1013 ± 8 hPa over 12 hours */
    float pressure = 1013.25f + 8.0f * sinf((hour / 12.0f) * 2.0f * M_PI);

    *out_hpa = add_jitter(pressure, 0.5f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_co2(float *out_ppm)
{
    if (!out_ppm) return ESP_ERR_INVALID_ARG;

    int64_t sim_tod = get_sim_time_of_day();
    float occupancy = get_occupancy_factor(sim_tod);

    /* Outdoor baseline 420ppm, rises to 800-1200ppm with occupancy */
    float base_co2 = 420.0f + (occupancy * 600.0f);

    /* Add slow drift (poor ventilation accumulation) */
    float hour = (float)sim_tod / 3600.0f;
    base_co2 += fminf(hour * 15.0f, 200.0f);  // Max +200ppm drift

    *out_ppm = add_jitter(base_co2, 40.0f);
    return ESP_OK;
}

esp_err_t sensor_sim_read_voc_nox(uint16_t *out_voc, uint16_t *out_nox)
{
    if (!out_voc || !out_nox) return ESP_ERR_INVALID_ARG;

    int64_t sim_tod = get_sim_time_of_day();
    float occupancy = get_occupancy_factor(sim_tod);

    /* VOC rises with cooking, cleaning, occupancy */
    /* Baseline 100, up to 250 with high activity */
    float voc = 100.0f + (occupancy * 100.0f);

    /* NOx stays low indoors (outdoor pollution source) */
    float nox = 110.0f + (occupancy * 20.0f);

    *out_voc = (uint16_t)add_jitter(voc, 20.0f);
    *out_nox = (uint16_t)add_jitter(nox, 15.0f);

    return ESP_OK;
}

/* Helper: calculate PM spike with realistic rise and exponential decay */
static float calc_cooking_event(int64_t sim_tod, int64_t event_start, int cook_duration, float peak_pm, float decay_time)
{
    int64_t elapsed = sim_tod - event_start;
    if (elapsed < 0) return 0.0f;

    if (elapsed < cook_duration) {
        /* Linear ramp-up during cooking */
        float progress = (float)elapsed / (float)cook_duration;
        return peak_pm * progress;
    } else {
        /* Exponential decay after cooking */
        int64_t decay_elapsed = elapsed - cook_duration;
        if (decay_elapsed > (decay_time * 4)) return 0.0f;  // Fully decayed (4x time constant)
        float decay_rate = 1.0f / (float)decay_time;
        return peak_pm * expf(-decay_rate * (float)decay_elapsed);
    }
}

esp_err_t sensor_sim_read_pm(float *out_pm1, float *out_pm25, float *out_pm10)
{
    if (!out_pm1 || !out_pm25 || !out_pm10) return ESP_ERR_INVALID_ARG;

    int64_t sim_tod = get_sim_time_of_day();
    float pm25_base = 8.0f;  // Clean indoor air baseline

    /* Three cooking events per simulated day:
     * Breakfast:  8:00 AM (28800s) - light cooking (toast, eggs), 5min active, 30min decay
     * Lunch:     12:30 PM (45000s) - medium cooking (pan frying), 8min active, 45min decay
     * Dinner:     6:30 PM (66600s) - heavy cooking (stir fry, multiple dishes), 12min active, 60min decay
     */

    float breakfast_spike = calc_cooking_event(sim_tod, 28800, 300, 30.0f, 1800);
    float lunch_spike = calc_cooking_event(sim_tod, 45000, 480, 70.0f, 2700);
    float dinner_spike = calc_cooking_event(sim_tod, 66600, 720, 110.0f, 3600);

    /* Use maximum of all events (only one typically active at a time) */
    float pm25 = pm25_base + fmaxf(fmaxf(breakfast_spike, lunch_spike), dinner_spike);

    *out_pm1 = add_jitter(pm25 * 0.7f, 1.0f);
    *out_pm25 = add_jitter(pm25, 2.0f);
    *out_pm10 = add_jitter(pm25 * 1.3f, 3.0f);

    return ESP_OK;
}

#endif /* CONFIG_IAQ_SIMULATION */
