/* components/display_oled/display_util.c */
#include "display_oled/display_util.h"
#include "sensor_coordinator.h"
#include "iaq_data.h"
#include "icons.h"

#include <string.h>
#include <math.h>
#include <stdio.h>
#include "sdkconfig.h"
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

/* Consolidated sensor status cache - single iteration updates all values */
typedef struct {
    bool any_warming;
    uint8_t progress;
    const char *status_text;
    int64_t last_tick;
} sensor_status_cache_t;

static sensor_status_cache_t s_sensor_cache = {
    .any_warming = false,
    .progress = 100,
    .status_text = "INIT",
    .last_tick = -1
};

static void update_sensor_status_cache(void)
{
    int64_t now_us = esp_timer_get_time();
    int64_t now_ms = now_us / 1000;
    /* Align cache updates to the configured display refresh cadence */
    int64_t tick_ms = (int64_t)CONFIG_IAQ_OLED_REFRESH_MS;
    if (tick_ms <= 0) tick_ms = 1000; /* safety */
    int64_t tick = now_ms / tick_ms;
    if (tick == s_sensor_cache.last_tick) {
        return;  /* Same cadence slot, reuse cache */
    }
    s_sensor_cache.last_tick = tick;

    /* Single iteration to compute all values */
    bool warming_found = false;
    bool has_error = false;
    bool has_init = false;
    bool has_uninit = false;
    bool all_ready = true;
    int64_t max_remaining_us = 0;
    int64_t max_total_us = 1;  /* Avoid divide by zero */

    for (sensor_id_t id = 0; id < SENSOR_ID_MAX; id++) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info(id, &info) != ESP_OK) {
            continue;
        }

        if (info.state != SENSOR_STATE_READY) {
            all_ready = false;
        }

        switch (info.state) {
            case SENSOR_STATE_ERROR:
                has_error = true;
                break;
            case SENSOR_STATE_WARMING: {
                warming_found = true;
                int64_t remaining = info.warmup_deadline_us - now_us;
                if (remaining < 0) remaining = 0;
                int64_t total = (int64_t)sensor_coordinator_get_warmup_ms(id) * 1000LL;
                if (remaining > max_remaining_us) {
                    max_remaining_us = remaining;
                    max_total_us = total;
                }
                break;
            }
            case SENSOR_STATE_INIT:
                has_init = true;
                break;
            case SENSOR_STATE_UNINIT:
                has_uninit = true;
                break;
            default:
                break;
        }
    }

    /* Update cached any_warming */
    s_sensor_cache.any_warming = warming_found;

    /* Update cached progress */
    if (!warming_found) {
        s_sensor_cache.progress = 100;
    } else {
        int64_t elapsed = max_total_us - max_remaining_us;
        if (elapsed < 0) elapsed = 0;
        if (max_total_us <= 0) {
            s_sensor_cache.progress = 0;
        } else {
            uint8_t pct = (uint8_t)((elapsed * 100) / max_total_us);
            if (pct > 100) pct = 100;
            s_sensor_cache.progress = pct;
        }
    }

    /* Update cached status text (priority: ERROR > WARMING > INIT > UNINIT > READY) */
    if (has_error) {
        s_sensor_cache.status_text = "ERROR";
    } else if (warming_found) {
        s_sensor_cache.status_text = "WARMING";
    } else if (has_init) {
        s_sensor_cache.status_text = "INIT";
    } else if (has_uninit) {
        s_sensor_cache.status_text = "UNINIT";
    } else if (all_ready) {
        s_sensor_cache.status_text = "READY";
    }
}

bool any_sensor_warming(void)
{
    update_sensor_status_cache();
    return s_sensor_cache.any_warming;
}

uint8_t get_warming_progress(void)
{
    update_sensor_status_cache();
    return s_sensor_cache.progress;
}

const char* get_sensor_status_text(void)
{
    update_sensor_status_cache();
    return s_sensor_cache.status_text;
}
