/* components/display_oled/display_ui.c */
#include "display_oled/display_ui.h"
#include "display_oled/display_driver.h"
#include "display_oled/display_graphics.h"
#include "display_oled/display_input.h"
#include "display_oled/display_util.h"

#include "iaq_config.h"
#include "iaq_data.h"
#include "system_context.h"
#include "time_sync.h"
#include "sensor_coordinator.h"
#include "icons.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <math.h>
#include <string.h>

/* Fonts (u8x8) */
#include "fonts/u8x8_font_chroma48medium8_r.h"
#include "fonts/u8x8_font_amstrad_cpc_extended_r.h"

#if CONFIG_IAQ_OLED_ENABLE
static const char *TAG = "OLED_UI";
#endif

#if CONFIG_IAQ_OLED_ENABLE

typedef void (*screen_render_fn_t)(uint8_t page, uint8_t *buf, bool full_redraw);

typedef struct {
    screen_render_fn_t render;
    const char *name;
    uint16_t refresh_ms; /* 0 = global */
} screen_t;

/* Screen cache for dirty tracking */
typedef struct {
    float co2;            // threshold: 10 ppm
    float temp;           // threshold: 0.1 C
    float pm25;           // threshold: 1 µg/m³
    int16_t aqi;          // threshold: 2
    bool wifi;
    bool mqtt;
    bool time_synced;
    bool warming;
    uint8_t last_hour;    // For time change detection
    uint8_t last_min;
    uint8_t last_sec;
    uint16_t page_hash[8];  // Rolling hash per page
} screen_cache_t;

#define DISPLAY_ERROR_THRESHOLD      3
#define DISPLAY_RETRY_INITIAL_MS  30000
#define DISPLAY_RETRY_MAX_MS     300000

typedef enum {
    DISPLAY_DRV_STATE_UNINIT = 0,
    DISPLAY_DRV_STATE_READY,
    DISPLAY_DRV_STATE_ERROR,
} display_driver_state_t;

typedef struct {
    display_driver_state_t state;
    uint32_t error_count;
    uint32_t retry_delay_ms;
    int64_t next_retry_us;
} display_driver_health_t;

static iaq_system_context_t *s_ctx = NULL;
static TaskHandle_t s_task = NULL;
static esp_timer_handle_t s_wake_timer = NULL;
static volatile bool s_wake_active = false;
static volatile bool s_enabled = true;
static volatile int s_screen_idx = 0;
static volatile int64_t s_last_activity_us = 0;
static display_font_t s_font_large = { .u8x8_font = u8x8_font_chroma48medium8_r };
static display_font_t s_font_label = { .u8x8_font = u8x8_font_amstrad_cpc_extended_r };
static bool s_invert = false;
static screen_cache_t s_cache[6];
static volatile bool s_force_redraw = false;
static display_driver_health_t s_driver_health = {
    .state = DISPLAY_DRV_STATE_UNINIT,
    .error_count = 0,
    .retry_delay_ms = DISPLAY_RETRY_INITIAL_MS,
    .next_retry_us = 0,
};

static void display_health_record_success(void);
static void display_health_report_failure(const char *scope, esp_err_t err);
static void display_health_try_recover(void);

/* Forward renders */
static void render_overview(uint8_t page, uint8_t *buf, bool full);
static void render_environment(uint8_t page, uint8_t *buf, bool full);
static void render_air_quality(uint8_t page, uint8_t *buf, bool full);
static void render_co2_detail(uint8_t page, uint8_t *buf, bool full);
static void render_particulate(uint8_t page, uint8_t *buf, bool full);
static void render_system(uint8_t page, uint8_t *buf, bool full);

static const screen_t s_screens[] = {
    { render_overview,     "Overview",    0 },
    { render_environment,  "Environment", 0 },
    { render_air_quality,  "Air Quality", 0 },
    { render_co2_detail,   "CO2",         0 },
    { render_particulate,  "PM",          0 },
    { render_system,       "System",      1000 },
};

#define NUM_SCREENS (sizeof(s_screens) / sizeof(s_screens[0]))

static inline uint16_t get_refresh_ms(void)
{
    uint16_t ms = s_screens[s_screen_idx].refresh_ms;
    return ms ? ms : (uint16_t)CONFIG_IAQ_OLED_REFRESH_MS;
}

static bool is_night_now(void)
{
    /* Require time synced */
    if (!s_ctx || !s_ctx->event_group) return false;
    EventBits_t bits = xEventGroupGetBits(s_ctx->event_group);
    if ((bits & TIME_SYNCED_BIT) == 0) return false;

    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    int start = CONFIG_IAQ_OLED_NIGHT_START_H;
    int end   = CONFIG_IAQ_OLED_NIGHT_END_H;
    if (start == end) return false; /* disabled */
    if (start < end) {
        return (t.tm_hour >= start && t.tm_hour < end);
    } else {
        return (t.tm_hour >= start || t.tm_hour < end);
    }
}

static void mark_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

static void wake_timer_callback(void *arg)
{
    (void)arg;
    display_ui_set_enabled(false);
    s_invert = false;
    if (s_driver_health.state == DISPLAY_DRV_STATE_READY) {
        esp_err_t err = display_driver_set_invert(false);
        if (err != ESP_OK) {
            display_health_report_failure("set_invert", err);
        }
    }
    s_wake_active = false;
}

static void display_health_record_success(void)
{
    bool was_ready = (s_driver_health.state == DISPLAY_DRV_STATE_READY);
    s_driver_health.state = DISPLAY_DRV_STATE_READY;
    s_driver_health.error_count = 0;
    s_driver_health.retry_delay_ms = DISPLAY_RETRY_INITIAL_MS;
    s_driver_health.next_retry_us = 0;
    if (!was_ready) {
        ESP_LOGI(TAG, "Display driver ready");
    }
}

static void display_health_report_failure(const char *scope, esp_err_t err)
{
    int64_t now = esp_timer_get_time();
    if (s_driver_health.error_count < DISPLAY_ERROR_THRESHOLD) {
        s_driver_health.error_count++;
    }
    ESP_LOGW(TAG, "Display %s failed: %s (%u/%u)", scope, esp_err_to_name(err),
             (unsigned)s_driver_health.error_count, DISPLAY_ERROR_THRESHOLD);

    if (s_driver_health.error_count >= DISPLAY_ERROR_THRESHOLD) {
        if (s_driver_health.state != DISPLAY_DRV_STATE_ERROR) {
            s_driver_health.state = DISPLAY_DRV_STATE_ERROR;
            s_driver_health.next_retry_us = now + (int64_t)s_driver_health.retry_delay_ms * 1000;
            ESP_LOGW(TAG, "Display entered ERROR state; retry in %u ms", s_driver_health.retry_delay_ms);
        }
        s_driver_health.error_count = DISPLAY_ERROR_THRESHOLD;
    }
}

static void display_health_try_recover(void)
{
    if (s_driver_health.state != DISPLAY_DRV_STATE_ERROR) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (s_driver_health.next_retry_us != 0 && now < s_driver_health.next_retry_us) {
        return;
    }

    esp_err_t err = display_driver_reset();
    if (err == ESP_OK) {
        display_health_record_success();
        s_force_redraw = true;
        if (!s_enabled) {
            (void)display_driver_power(false);
        }
        return;
    }

    if (s_driver_health.retry_delay_ms < DISPLAY_RETRY_MAX_MS) {
        uint32_t next = s_driver_health.retry_delay_ms * 2;
        if (next > DISPLAY_RETRY_MAX_MS) next = DISPLAY_RETRY_MAX_MS;
        s_driver_health.retry_delay_ms = next;
    }
    s_driver_health.next_retry_us = now + (int64_t)s_driver_health.retry_delay_ms * 1000;
    ESP_LOGW(TAG, "Display recovery failed: %s (retry in %u ms)",
             esp_err_to_name(err), s_driver_health.retry_delay_ms);
}

void display_ui_wake_for_seconds(uint32_t seconds)
{
    display_ui_set_enabled(true);
    s_wake_active = true;
    if (s_wake_timer) {
        /* Restart the one-shot timer */
        esp_timer_stop(s_wake_timer);
        esp_timer_start_once(s_wake_timer, (uint64_t)seconds * 1000000ULL);
    }
}

void display_ui_set_enabled(bool on)
{
    s_enabled = on;
    if (on) {
        mark_activity();
        s_force_redraw = true;
    }

    if (on && s_driver_health.state == DISPLAY_DRV_STATE_ERROR) {
        ESP_LOGW(TAG, "Display enable requested while driver recovering; deferring power-on");
        return;
    }

    esp_err_t err = display_driver_power(on);
    if (err != ESP_OK) {
        display_health_report_failure("power", err);
        return;
    }

    if (on) {
        display_health_record_success();
    }
}

bool display_ui_is_enabled(void) { return s_enabled; }

void display_ui_next_screen(void)
{
    s_screen_idx = (s_screen_idx + 1) % (int)NUM_SCREENS;
    mark_activity();
    s_force_redraw = true;
}

void display_ui_prev_screen(void)
{
    s_screen_idx = (s_screen_idx - 1);
    if (s_screen_idx < 0) s_screen_idx = (int)NUM_SCREENS - 1;
    mark_activity();
    s_force_redraw = true;
}

/* Check if screen data has changed enough to require redraw */
static bool check_screen_dirty(int idx)
{
    bool dirty = s_force_redraw;

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();

        /* CO2 threshold: 10 ppm */
        if (d->valid.co2_ppm && fabsf(d->fused.co2_ppm - s_cache[idx].co2) > 10.0f) {
            s_cache[idx].co2 = d->fused.co2_ppm;
            dirty = true;
        }

        /* Temperature threshold: 0.1 C */
        if (d->valid.temp_c && fabsf(d->fused.temp_c - s_cache[idx].temp) > 0.1f) {
            s_cache[idx].temp = d->fused.temp_c;
            dirty = true;
        }

        /* PM2.5 threshold: 1 µg/m³ */
        if (d->valid.pm25_ugm3 && fabsf(d->fused.pm25_ugm3 - s_cache[idx].pm25) > 1.0f) {
            s_cache[idx].pm25 = d->fused.pm25_ugm3;
            dirty = true;
        }

        /* AQI threshold: 2 */
        if (abs((int)d->metrics.aqi_value - (int)s_cache[idx].aqi) > 2) {
            s_cache[idx].aqi = (int16_t)d->metrics.aqi_value;
            dirty = true;
        }

        /* Boolean changes always trigger redraw */
        if (d->system.wifi_connected != s_cache[idx].wifi) {
            s_cache[idx].wifi = d->system.wifi_connected;
            dirty = true;
        }

        if (d->system.mqtt_connected != s_cache[idx].mqtt) {
            s_cache[idx].mqtt = d->system.mqtt_connected;
            dirty = true;
        }
    }

    /* Check time sync status */
    if (s_ctx && s_ctx->event_group) {
        EventBits_t bits = xEventGroupGetBits(s_ctx->event_group);
        bool synced = (bits & TIME_SYNCED_BIT) != 0;
        if (synced != s_cache[idx].time_synced) {
            s_cache[idx].time_synced = synced;
            dirty = true;
        }

        /* Check time value changes (only on screens that display time) */
        /* Overview (idx=0) shows time on pages 4-5, System (idx=5) shows time on page 3 */
        if (synced && (idx == 0 || idx == 5)) {
            time_t now; time(&now);
            struct tm t; localtime_r(&now, &t);

            /* Check if second changed */
            if (t.tm_sec != s_cache[idx].last_sec) {
                s_cache[idx].last_hour = (uint8_t)t.tm_hour;
                s_cache[idx].last_min = (uint8_t)t.tm_min;
                s_cache[idx].last_sec = (uint8_t)t.tm_sec;
                dirty = true;
            }
        }
    }

    /* Check warming status (cached) */
    bool warming = any_sensor_warming();
    if (warming != s_cache[idx].warming) {
        s_cache[idx].warming = warming;
        dirty = true;
    }

    return dirty;
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t page_buf[128];
    static int last_drawn_screen = -1;

    for (;;) {
        display_health_try_recover();

        if (!s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Handle input events */
        display_button_event_t ev = display_input_poll_event();

        /* Night mode handling: allow rendering only if woken */
        if (is_night_now() && !s_wake_active) {
            if (ev == DISPLAY_BTN_EVENT_SHORT) {
                display_ui_wake_for_seconds(CONFIG_IAQ_OLED_WAKE_SECS);
            }
            /* Ignore long press at night */
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Day mode button handling */
        if (ev == DISPLAY_BTN_EVENT_SHORT) {
            display_ui_next_screen();
        } else if (ev == DISPLAY_BTN_EVENT_LONG) {
            if (s_driver_health.state == DISPLAY_DRV_STATE_READY) {
                bool desired_invert = !s_invert;
                esp_err_t err = display_driver_set_invert(desired_invert);
                if (err == ESP_OK) {
                    s_invert = desired_invert;
                    display_health_record_success();
                } else {
                    display_health_report_failure("set_invert", err);
                    s_force_redraw = true;
                }
            } else {
                ESP_LOGW(TAG, "Invert toggle ignored: display driver not ready");
            }
            mark_activity();
        }

        /* Auto-off if idle */
        int idle_ms = CONFIG_IAQ_OLED_IDLE_TIMEOUT_MS;
        if (idle_ms > 0) {
            int64_t now = esp_timer_get_time();
            if (now - s_last_activity_us > (int64_t)idle_ms * 1000) {
                display_ui_set_enabled(false);
                continue;
            }
        }

        if (s_driver_health.state == DISPLAY_DRV_STATE_ERROR) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Check if screen needs redraw */
        int idx = s_screen_idx;
        bool screen_changed = (idx != last_drawn_screen);
        bool needs_redraw = check_screen_dirty(idx) || screen_changed;

        if (!needs_redraw) {
            /* Fast poll without redraw */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Clear force flag after use */
        bool force_page_write = screen_changed;
        bool frame_failed = false;
        s_force_redraw = false;

        /* Render all pages with hash skip */
        for (uint8_t page = 0; page < 8; ++page) {
            display_gfx_clear(page_buf);
            s_screens[idx].render(page, page_buf, true);

            /* Check page hash - skip I2C write if unchanged */
            uint16_t hash = display_gfx_page_hash(page_buf);
            if (force_page_write || hash != s_cache[idx].page_hash[page]) {
                esp_err_t err = display_driver_write_page(page, page_buf);
                if (err == ESP_OK) {
                    s_cache[idx].page_hash[page] = hash;
                } else {
                    frame_failed = true;
                    display_health_report_failure("write_page", err);
                    s_force_redraw = true;
                    break;
                }
            }
        }

        if (frame_failed) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        display_health_record_success();

        /* Sleep until next refresh check */
        vTaskDelay(pdMS_TO_TICKS(get_refresh_ms()));

        /* Update last drawn screen after a render pass */
        last_drawn_screen = idx;
    }
}

/* ===== Screen Implementations ===== */

static void render_overview(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;

    char str[32];
    float co2 = NAN, pm25 = NAN, temp = NAN, rh = NAN, pressure_pa = NAN;
    uint16_t aqi = 0;
    bool wifi = false, mqtt = false, time_synced = false;
    int hour = 0, min = 0, sec = 0;

    /* Copy data under lock */
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        if (d->valid.co2_ppm) co2 = d->fused.co2_ppm;
        if (d->valid.pm25_ugm3) pm25 = d->fused.pm25_ugm3;
        if (d->valid.temp_c) temp = d->fused.temp_c;
        if (d->valid.rh_pct) rh = d->fused.rh_pct;
        if (d->valid.pressure_pa) pressure_pa = d->fused.pressure_pa;
        aqi = d->metrics.aqi_value;
        wifi = d->system.wifi_connected;
        mqtt = d->system.mqtt_connected;
    }

    if (s_ctx && s_ctx->event_group) {
        EventBits_t bits = xEventGroupGetBits(s_ctx->event_group);
        time_synced = (bits & TIME_SYNCED_BIT) != 0;
    }

    if (time_synced) {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        hour = t.tm_hour;
        min = t.tm_min;
        sec = t.tm_sec;
    }

    /* Page 0: Time (8×8, left) + WiFi/MQTT icons (right) */
    if (page == 0) {
        if (time_synced) {
            snprintf(str, sizeof(str), "%02d:%02d:%02d", hour, min, sec);
        } else {
            snprintf(str, sizeof(str), "--:--:--");
        }
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, str, &s_font_label);

        /* Status icons on right */
        display_draw_icon_at(page, buf, 96, 0, wifi ? ICON_WIFI : ICON_WIFI_OFF, false);
        display_draw_icon_at(page, buf, 112, 0, mqtt ? ICON_MQTT : ICON_MQTT_OFF, false);
    }

    /* Page 1: CO2 (8×8): "CO2: 1234 ppm" */
    if (page == 1) {
        char co2_str[12];
        fmt_float(co2_str, sizeof(co2_str), co2, 0, "---");
        snprintf(str, sizeof(str), "CO2:%s ppm", co2_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 8, str, &s_font_label);
    }

    /* Page 2: AQI (show "--" if UINT16_MAX or 0) */
    if (page == 2) {
        if (aqi == UINT16_MAX || aqi == 0) {
            snprintf(str, sizeof(str), "AQI: --");
        } else {
            snprintf(str, sizeof(str), "AQI:%u %s", aqi, get_aqi_short(aqi));
        }
        display_gfx_draw_text_8x8_page(page, buf, 0, 16, str, &s_font_label);
    }

    /* Page 3: PM2.5 with 1 decimal */
    if (page == 3) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), pm25, 1, "---");
        snprintf(str, sizeof(str), "PM2.5:%s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: Temperature with C */
    if (page == 4) {
        char temp_str[12];
        fmt_float(temp_str, sizeof(temp_str), temp, 1, "--");
        snprintf(str, sizeof(str), "Temp:%s C", temp_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: Humidity with % */
    if (page == 5) {
        char rh_str[12];
        fmt_float(rh_str, sizeof(rh_str), rh, 1, "--");
        snprintf(str, sizeof(str), "RH:%s %%", rh_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: Pressure with hPa and 1 decimal */
    if (page == 6) {
        char press_str[12];
        float pressure_hpa = pressure_pa / 100.0f;  /* Convert Pa to hPa */
        fmt_float(press_str, sizeof(press_str), pressure_hpa, 1, "----");
        snprintf(str, sizeof(str), "P:%s hPa", press_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: Sensor status with progress bar during warming */
    if (page == 7) {
        const char *status = get_sensor_status_text();
        uint8_t progress = get_warming_progress();

        /* Draw progress bar with status text overlay */
        display_gfx_draw_progress_bar(buf, 0, 128, progress, status, &s_font_label);
    }
}

static void render_environment(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;

    char str[32];
    float temp = NAN, rh = NAN, pressure = NAN, dewpt = NAN;
    int comfort = 0, mold = 0;
    const char *comfort_cat = "", *mold_cat = "";
    pressure_trend_t trend = PRESSURE_TREND_UNKNOWN;

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        if (d->valid.temp_c) temp = d->fused.temp_c;
        if (d->valid.rh_pct) rh = d->fused.rh_pct;
        if (d->valid.pressure_pa) pressure = d->fused.pressure_pa / 100.0f; // Pa → hPa
        dewpt = d->metrics.dew_point_c;
        comfort = d->metrics.comfort_score;
        comfort_cat = d->metrics.comfort_category;
        mold = d->metrics.mold_risk_score;
        mold_cat = d->metrics.mold_risk_category;
        trend = d->metrics.pressure_trend;
    }

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "Environment", &s_font_label);
    }

    /* Page 1-2: Temperature (8×16) */
    if (page >= 1 && page <= 2) {
        char tmp[16];
        fmt_float(tmp, sizeof(tmp), temp, 1, "---");
        snprintf(str, sizeof(str), "%s C", tmp);
        display_gfx_draw_text_8x16_page(page, buf, 0, 8, str, &s_font_large);
    }

    /* Page 3: RH + Dewpoint */
    if (page == 3) {
        char rh_str[12], dew_str[12];
        fmt_float(rh_str, sizeof(rh_str), rh, 1, "--");
        fmt_float(dew_str, sizeof(dew_str), dewpt, 1, "--");
        snprintf(str, sizeof(str), "RH:%s%% Dew:%s", rh_str, dew_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: Pressure + trend icon */
    if (page == 4) {
        char p_str[12];
        fmt_float(p_str, sizeof(p_str), pressure, 0, "----");
        snprintf(str, sizeof(str), "P:%s hPa", p_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
        display_draw_icon_at(page, buf, 100, 32, get_pressure_trend_icon(trend), false);
    }

    /* Page 5: Comfort */
    if (page == 5) {
        snprintf(str, sizeof(str), "Comfort:%d %s", comfort, comfort_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: Mold risk */
    if (page == 6) {
        snprintf(str, sizeof(str), "Mold:%d %s", mold, mold_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }
}

static void render_air_quality(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;

    char str[32];
    uint16_t aqi = 0;
    float pm25 = NAN, pm10 = NAN;
    const char *aqi_cat = "", *voc_cat = "", *nox_cat = "";
    int iaq_score = 0;

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        aqi = d->metrics.aqi_value;
        aqi_cat = d->metrics.aqi_category;
        if (d->valid.pm25_ugm3) pm25 = d->fused.pm25_ugm3;
        if (d->valid.pm10_ugm3) pm10 = d->fused.pm10_ugm3;
        voc_cat = d->metrics.voc_category;
        nox_cat = d->metrics.nox_category;
        iaq_score = d->metrics.overall_iaq_score;
    }

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "Air Quality", &s_font_label);
    }

    /* Page 1-2: AQI (8×16) */
    if (page >= 1 && page <= 2) {
        snprintf(str, sizeof(str), "AQI:%u", aqi);
        display_gfx_draw_text_8x16_page(page, buf, 0, 8, str, &s_font_large);
    }

    /* Page 3: AQI category + PM values */
    if (page == 3) {
        snprintf(str, sizeof(str), "%s", aqi_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: PM2.5 bar (0-50 scale) with label on the same page (left label, right bar) */
    if (page == 4) {
        /* Reserve left area for label text (9 chars * 8px ~= 72px). Bar starts after label. */
        const int label_x = 0;
        const int bar_x = 72;                 /* start of bar area */
        const int bar_w_max = 128 - bar_x;    /* available width for bar */

        /* Label */
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), pm25, 0, "--");
        snprintf(str, sizeof(str), "PM2.5:%s", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, label_x, 32, str, &s_font_label);

        /* Bar mapped to available region */
        int bar_width = (int)(pm25 * (float)bar_w_max / 50.0f);
        if (bar_width > bar_w_max) bar_width = bar_w_max;
        if (bar_width < 0) bar_width = 0;
        display_gfx_draw_hbar(buf, bar_x, bar_width, 0xFF);  /* full page height */
    }

    /* Page 5: PM10 bar (0-100 scale) with label on the same page (left label, right bar) */
    if (page == 5) {
        const int label_x = 0;
        const int bar_x = 72;                 /* start of bar area */
        const int bar_w_max = 128 - bar_x;    /* available width for bar */

        /* Label */
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), pm10, 0, "--");
        snprintf(str, sizeof(str), "PM10:%s", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, label_x, 40, str, &s_font_label);

        /* Bar mapped to available region */
        int bar_width = (int)(pm10 * (float)bar_w_max / 100.0f);
        if (bar_width > bar_w_max) bar_width = bar_w_max;
        if (bar_width < 0) bar_width = 0;
        display_gfx_draw_hbar(buf, bar_x, bar_width, 0xFF);  /* full page height */
    }

    /* Page 6: VOC + NOx */
    if (page == 6) {
        snprintf(str, sizeof(str), "VOC:%s NOx:%s", voc_cat, nox_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: IAQ score */
    if (page == 7) {
        snprintf(str, sizeof(str), "IAQ:%d/100", iaq_score);
        display_gfx_draw_text_8x8_page(page, buf, 0, 56, str, &s_font_label);
    }
}

static void render_co2_detail(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;

    char str[32];
    float co2 = NAN, rate = NAN;
    int score = 0;
    uint16_t abc_baseline = 0;
    uint8_t abc_conf = 0;
    bool s8_valid = false;

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        if (d->valid.co2_ppm) co2 = d->fused.co2_ppm;
        rate = d->metrics.co2_rate_ppm_hr;
        score = d->metrics.co2_score;
        abc_baseline = d->fusion_diag.co2_abc_baseline_ppm;
        abc_conf = d->fusion_diag.co2_abc_confidence_pct;
        s8_valid = d->hw_diag.s8_diag_valid;
    }

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "CO2 Detail", &s_font_label);
    }

    /* Page 1-3: Large CO2 (8×16, spans 2 pages) */
    if (page >= 1 && page <= 3) {
        char tmp[16];
        fmt_float(tmp, sizeof(tmp), co2, 0, "---");
        snprintf(str, sizeof(str), "%s ppm", tmp);
        display_gfx_draw_text_8x16_page(page, buf, 0, 8, str, &s_font_large);
    }

    /* Page 4: Rate */
    if (page == 4) {
        char rate_str[12];
        fmt_float(rate_str, sizeof(rate_str), rate, 0, "---");
        snprintf(str, sizeof(str), "Rate:%s ppm/h", rate_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: Score */
    if (page == 5) {
        snprintf(str, sizeof(str), "Score:%d/100", score);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: ABC info */
    if (page == 6) {
        snprintf(str, sizeof(str), "ABC:%u (%u%%)", abc_baseline, abc_conf);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: S8 status */
    if (page == 7) {
        snprintf(str, sizeof(str), "S8:%s", s8_valid ? "OK" : "N/A");
        display_gfx_draw_text_8x8_page(page, buf, 0, 56, str, &s_font_label);
    }
}

static void render_particulate(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;

    char str[32];
    float pm1 = NAN, pm25 = NAN, pm10 = NAN, ratio = NAN;
    int quality = 0;
    bool spike = false;

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        if (d->valid.pm1_ugm3) pm1 = d->fused.pm1_ugm3;
        if (d->valid.pm25_ugm3) pm25 = d->fused.pm25_ugm3;
        if (d->valid.pm10_ugm3) pm10 = d->fused.pm10_ugm3;
        quality = d->fusion_diag.pm25_quality;
        ratio = d->fusion_diag.pm1_pm25_ratio;
        spike = d->metrics.pm25_spike_detected;
    }

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "Particulate", &s_font_label);
        if (spike) {
            display_draw_icon_at(page, buf, 112, 0, ICON_ALERT, false);
        }
    }

    /* Page 1: PM1.0 */
    if (page == 1) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), pm1, 0, "---");
        snprintf(str, sizeof(str), "PM1.0: %s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 8, str, &s_font_label);
    }

    /* Page 2: PM2.5 */
    if (page == 2) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), pm25, 0, "---");
        snprintf(str, sizeof(str), "PM2.5: %s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 16, str, &s_font_label);
    }

    /* Page 3: PM10 */
    if (page == 3) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), pm10, 0, "---");
        snprintf(str, sizeof(str), "PM10:  %s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: Quality */
    if (page == 4) {
        snprintf(str, sizeof(str), "Quality: %d%%", quality);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: PM1/PM2.5 ratio */
    if (page == 5) {
        char ratio_str[12];
        fmt_float(ratio_str, sizeof(ratio_str), ratio, 2, "---");
        snprintf(str, sizeof(str), "PM1/PM2.5: %s", ratio_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }
}

static void render_system(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;

    char str[64];
    bool wifi = false, mqtt = false, time_synced = false;
    int32_t rssi = 0;
    uint32_t uptime = 0, heap = 0, min_heap = 0;
    int hour = 0, min = 0, sec = 0;

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        wifi = d->system.wifi_connected;
        mqtt = d->system.mqtt_connected;
        rssi = d->system.wifi_rssi;
        uptime = d->system.uptime_seconds;
        heap = d->system.free_heap / 1024;
        min_heap = d->system.min_free_heap / 1024;
    }

    if (s_ctx && s_ctx->event_group) {
        EventBits_t bits = xEventGroupGetBits(s_ctx->event_group);
        time_synced = (bits & TIME_SYNCED_BIT) != 0;
    }

    if (time_synced) {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        hour = t.tm_hour;
        min = t.tm_min;
        sec = t.tm_sec;
    }

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "System", &s_font_label);
    }

    /* Page 1: WiFi */
    if (page == 1) {
        display_draw_icon_at(page, buf, 0, 8, wifi ? ICON_WIFI : ICON_WIFI_OFF, false);
        if (wifi) {
            snprintf(str, sizeof(str), "RSSI:%ld dBm", (long)rssi);
        } else {
            snprintf(str, sizeof(str), "Down");
        }
        display_gfx_draw_text_8x8_page(page, buf, 16, 8, str, &s_font_label);
    }

    /* Page 2: MQTT */
    if (page == 2) {
        display_draw_icon_at(page, buf, 0, 16, mqtt ? ICON_MQTT : ICON_MQTT_OFF, false);
        snprintf(str, sizeof(str), "%s", mqtt ? "Connected" : "Down");
        display_gfx_draw_text_8x8_page(page, buf, 16, 16, str, &s_font_label);
    }

    /* Page 3: Time */
    if (page == 3) {
        display_draw_icon_at(page, buf, 0, 24, ICON_CLOCK, false);
        if (time_synced) {
            snprintf(str, sizeof(str), "%02d:%02d:%02d", hour, min, sec);
        } else {
            snprintf(str, sizeof(str), "No sync");
        }
        display_gfx_draw_text_8x8_page(page, buf, 16, 24, str, &s_font_label);
    }

    /* Page 4: Uptime */
    if (page == 4) {
        char uptime_str[24];
        fmt_uptime(uptime_str, sizeof(uptime_str), uptime);
        snprintf(str, sizeof(str), "Up: %s", uptime_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: Heap */
    if (page == 5) {
        snprintf(str, sizeof(str), "Heap: %lu kB", (unsigned long)heap);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: Min heap */
    if (page == 6) {
        snprintf(str, sizeof(str), "Min: %lu kB", (unsigned long)min_heap);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: Sensor status */
    if (page == 7) {
        int ready = 0, total = SENSOR_ID_MAX;
        for (sensor_id_t id = 0; id < SENSOR_ID_MAX; id++) {
            sensor_runtime_info_t info;
            if (sensor_coordinator_get_runtime_info(id, &info) == ESP_OK) {
                if (info.state == SENSOR_STATE_READY) ready++;
            }
        }
        snprintf(str, sizeof(str), "Sensors: %d/%d", ready, total);
        display_gfx_draw_text_8x8_page(page, buf, 0, 56, str, &s_font_label);
    }
}

/* ===== Event Handler ===== */

static void iaq_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base != IAQ_EVENT) return;

    /* Set dirty flags for all screens on events */
    switch (id) {
        case IAQ_EVENT_WIFI_CONNECTED:
        case IAQ_EVENT_WIFI_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi event, marking screens dirty");
            for (int i = 0; i < (int)NUM_SCREENS; i++) {
                s_cache[i].wifi = !s_cache[i].wifi; // Toggle to force dirty
            }
            break;

        case IAQ_EVENT_TIME_SYNCED:
            ESP_LOGI(TAG, "Time synced, marking screens dirty");
            for (int i = 0; i < (int)NUM_SCREENS; i++) {
                s_cache[i].time_synced = !s_cache[i].time_synced;
            }
            break;

        default:
            break;
    }
}

/* ===== Public API ===== */

esp_err_t display_ui_init(iaq_system_context_t *ctx)
{
    s_ctx = ctx;
    if (!CONFIG_IAQ_OLED_ENABLE) {
        ESP_LOGI(TAG, "OLED disabled by Kconfig");
        return ESP_OK;
    }

    esp_err_t init_err = display_driver_init();
    if (init_err != ESP_OK) {
        ESP_LOGW(TAG, "Display driver init failed: %s", esp_err_to_name(init_err));
        display_health_report_failure("init", init_err);
    } else {
        display_health_record_success();
    }

    ESP_RETURN_ON_ERROR(display_input_init(), TAG, "input init failed");

    /* Create wake timer */
    esp_timer_create_args_t timer_cfg = {
        .callback = wake_timer_callback,
        .name = "oled_wake"
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_cfg, &s_wake_timer), TAG, "wake timer create failed");

    /* Register event handler */
    esp_err_t err = esp_event_handler_register(IAQ_EVENT, ESP_EVENT_ANY_ID, &iaq_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    /* Initialize cache */
    memset(s_cache, 0, sizeof(s_cache));

    s_enabled = true;
    s_last_activity_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Display UI initialized");
    return ESP_OK;
}

esp_err_t display_ui_start(void)
{
    if (!CONFIG_IAQ_OLED_ENABLE) return ESP_OK;
    if (s_task) return ESP_OK;

    BaseType_t ok = xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_DISPLAY,
                                            NULL, TASK_PRIORITY_DISPLAY, &s_task, TASK_CORE_DISPLAY);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Display task started (core %d, priority %d)", TASK_CORE_DISPLAY, TASK_PRIORITY_DISPLAY);
    return ESP_OK;
}

#else /* CONFIG_IAQ_OLED_ENABLE */

esp_err_t display_ui_init(iaq_system_context_t *ctx) { (void)ctx; return ESP_OK; }
esp_err_t display_ui_start(void) { return ESP_OK; }
void display_ui_set_enabled(bool on) { (void)on; }
bool display_ui_is_enabled(void) { return false; }
void display_ui_next_screen(void) {}
void display_ui_prev_screen(void) {}
void display_ui_wake_for_seconds(uint32_t seconds) { (void)seconds; }

#endif /* CONFIG_IAQ_OLED_ENABLE */
