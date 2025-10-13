/* components/display_oled/display_util.c */
#include "display_oled/display_util.h"
#include "sensor_coordinator.h"
#include "iaq_data.h"
#include "icons.h"

#include <string.h>
#include <math.h>
#include <stdio.h>
#include "esp_timer.h"

void fmt_float(char *buf, size_t len, float val, int decimals, const char *fallback)
{
    if (!buf || len == 0) return;

    /* Check for invalid values */
    if (isnan(val) || isinf(val)) {
        if (fallback) {
            snprintf(buf, len, "%s", fallback);
        } else {
            buf[0] = '\0';
        }
        return;
    }

    /* Use integer arithmetic to avoid float printf bloat */
    int multiplier = 1;
    if (decimals == 1) multiplier = 10;
    else if (decimals == 2) multiplier = 100;

    int int_val = (int)roundf(val * multiplier);

    if (decimals == 0) {
        snprintf(buf, len, "%d", int_val);
    } else if (decimals == 1) {
        int whole = int_val / 10;
        int frac = abs(int_val % 10);
        snprintf(buf, len, "%d.%d", whole, frac);
    } else if (decimals == 2) {
        int whole = int_val / 100;
        int frac = abs(int_val % 100);
        snprintf(buf, len, "%d.%02d", whole, frac);
    } else {
        /* Unsupported decimals, fallback */
        if (fallback) {
            snprintf(buf, len, "%s", fallback);
        } else {
            snprintf(buf, len, "%.1f", (double)val);
        }
    }
}

void fmt_int(char *buf, size_t len, int val, const char *fallback)
{
    if (!buf || len == 0) return;

    /* Check if fallback is needed (optional) */
    if (fallback && val < 0) {
        snprintf(buf, len, "%s", fallback);
    } else {
        snprintf(buf, len, "%d", val);
    }
}

void fmt_uptime(char *buf, size_t len, uint32_t seconds)
{
    if (!buf || len == 0) return;

    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;

    if (days > 0) {
        snprintf(buf, len, "%lud %luh %lum", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
    } else if (hours > 0) {
        snprintf(buf, len, "%luh %lum", (unsigned long)hours, (unsigned long)mins);
    } else {
        snprintf(buf, len, "%lum", (unsigned long)mins);
    }
}

const char* get_aqi_short(uint16_t aqi)
{
    if (aqi <= 50) return "Good";
    if (aqi <= 100) return "Mod";
    if (aqi <= 150) return "USG";
    if (aqi <= 200) return "Unhlt";
    if (aqi <= 300) return "V.Unhl";
    return "Hazrd";
}

const uint8_t* get_pressure_trend_icon(int trend)
{
    /* trend values from iaq_data.h: RISING=0, STABLE=1, FALLING=2, UNKNOWN=3 */
    if (trend == 0) return ICON_ARROW_UP;
    if (trend == 2) return ICON_ARROW_DOWN;
    return ICON_ARROW_STABLE;
}

bool any_sensor_warming(void)
{
    static int64_t last_check_us = 0;
    static bool cached_result = false;

    int64_t now = esp_timer_get_time();
    if (now - last_check_us < 1000000) {
        return cached_result;
    }

    last_check_us = now;
    cached_result = false;

    for (sensor_id_t id = 0; id < SENSOR_ID_MAX; id++) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info(id, &info) == ESP_OK) {
            if (info.state == SENSOR_STATE_WARMING) {
                cached_result = true;
                return true;
            }
        }
    }

    return false;
}

uint8_t get_warming_progress(void)
{
    /* Warmup durations from Kconfig (milliseconds) - must match sensor_coordinator.c */
    static const uint32_t warmup_ms[SENSOR_ID_MAX] = {
        [SENSOR_ID_MCU]     = CONFIG_IAQ_WARMUP_MCU_MS,
        [SENSOR_ID_SHT45]   = CONFIG_IAQ_WARMUP_SHT45_MS,
        [SENSOR_ID_BMP280]  = CONFIG_IAQ_WARMUP_BMP280_MS,
        [SENSOR_ID_SGP41]   = CONFIG_IAQ_WARMUP_SGP41_MS,
        [SENSOR_ID_PMS5003] = CONFIG_IAQ_WARMUP_PMS5003_MS,
        [SENSOR_ID_S8]      = CONFIG_IAQ_WARMUP_S8_MS,
    };

    int64_t now = esp_timer_get_time();
    int64_t max_remaining_us = 0;
    int64_t max_total_us = 1;  /* Avoid divide by zero */
    bool any_warming = false;

    for (sensor_id_t id = 0; id < SENSOR_ID_MAX; id++) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info(id, &info) == ESP_OK) {
            if (info.state == SENSOR_STATE_WARMING) {
                any_warming = true;
                int64_t remaining = info.warmup_deadline_us - now;
                if (remaining < 0) remaining = 0;

                /* Get total warmup time from config */
                int64_t total = (int64_t)warmup_ms[id] * 1000LL;

                /* Track sensor with longest remaining time */
                if (remaining > max_remaining_us) {
                    max_remaining_us = remaining;
                    max_total_us = total;
                }
            }
        }
    }

    if (!any_warming) {
        return 100;  /* Complete */
    }

    /* Calculate percentage: 100 * elapsed / total */
    int64_t elapsed = max_total_us - max_remaining_us;
    if (elapsed < 0) elapsed = 0;
    if (max_total_us <= 0) return 0;

    uint8_t pct = (uint8_t)((elapsed * 100) / max_total_us);
    if (pct > 100) pct = 100;

    return pct;
}

const char* get_sensor_status_text(void)
{
    static int64_t last_check_us = 0;
    static const char* cached_result = "INIT";

    int64_t now = esp_timer_get_time();
    if (now - last_check_us < 1000000) {
        return cached_result;
    }

    last_check_us = now;

    /* Priority: ERROR > WARMING > INIT > UNINIT, default to READY if all ready */
    bool has_error = false;
    bool has_warming = false;
    bool has_init = false;
    bool has_uninit = false;
    bool all_ready = true;

    for (sensor_id_t id = 0; id < SENSOR_ID_MAX; id++) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info(id, &info) == ESP_OK) {
            if (info.state != SENSOR_STATE_READY) {
                all_ready = false;
            }
            if (info.state == SENSOR_STATE_ERROR) {
                has_error = true;
            } else if (info.state == SENSOR_STATE_WARMING) {
                has_warming = true;
            } else if (info.state == SENSOR_STATE_INIT) {
                has_init = true;
            } else if (info.state == SENSOR_STATE_UNINIT) {
                has_uninit = true;
            }
        }
    }

    if (has_error) {
        cached_result = "ERROR";
    } else if (has_warming) {
        cached_result = "WARMING";
    } else if (has_init) {
        cached_result = "INIT";
    } else if (has_uninit) {
        cached_result = "UNINIT";
    } else if (all_ready) {
        cached_result = "READY";
    }

    return cached_result;
}
