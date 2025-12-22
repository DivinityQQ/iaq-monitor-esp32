/* components/display_oled/display_screens.c */
#include "display_oled/display_screens.h"
#include "display_oled/display_util.h"
#include "icons.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "sdkconfig.h"

#if CONFIG_IAQ_OLED_ENABLE

/* Fonts (u8x8) */
#include "fonts/u8x8_font_chroma48medium8_r.h"
#include "fonts/u8x8_font_amstrad_cpc_extended_r.h"

/* Font instances */
static const display_font_t s_font_large = { .u8x8_font = u8x8_font_chroma48medium8_r };
static const display_font_t s_font_label = { .u8x8_font = u8x8_font_amstrad_cpc_extended_r };

/* UI layout constants */
#define BAR_LABEL_WIDTH_PX   72   /* Width reserved for bar labels */

/* ===== Screen Implementations ===== */

void render_overview(uint8_t page, uint8_t *buf, const display_snapshot_t *snap)
{
    char str[32];

    /* Page 0: Time (8×8, left) + WiFi/MQTT icons (right) */
    if (page == 0) {
        if (snap->time_synced) {
            snprintf(str, sizeof(str), "%02d:%02d:%02d", snap->hour, snap->min, snap->sec);
        } else {
            snprintf(str, sizeof(str), "--:--:--");
        }
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, str, &s_font_label);

        /* Status icons on right */
        display_draw_icon_at(page, buf, 96, 0, snap->wifi ? ICON_WIFI : ICON_WIFI_OFF, false);
        display_draw_icon_at(page, buf, 112, 0, snap->mqtt ? ICON_MQTT : ICON_MQTT_OFF, false);
    }

    /* Page 1: CO2 (8×8): "CO2: 1234 ppm" */
    if (page == 1) {
        char co2_str[12];
        fmt_float(co2_str, sizeof(co2_str), snap->co2, 0, "---");
        snprintf(str, sizeof(str), "CO2:%s ppm", co2_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 8, str, &s_font_label);
    }

    /* Page 2: AQI (show "--" if UINT16_MAX or 0) */
    if (page == 2) {
        if (snap->aqi == UINT16_MAX) {
            snprintf(str, sizeof(str), "AQI: --");
        } else {
            snprintf(str, sizeof(str), "AQI:%u %s", snap->aqi, get_aqi_short(snap->aqi));
        }
        display_gfx_draw_text_8x8_page(page, buf, 0, 16, str, &s_font_label);
    }

    /* Page 3: PM2.5 with 1 decimal */
    if (page == 3) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), snap->pm25, 1, "---");
        snprintf(str, sizeof(str), "PM2.5:%s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: Temperature with C */
    if (page == 4) {
        char temp_str[12];
        fmt_float(temp_str, sizeof(temp_str), snap->temp, 1, "--");
        snprintf(str, sizeof(str), "Temp:%s C", temp_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: Humidity with % */
    if (page == 5) {
        char rh_str[12];
        fmt_float(rh_str, sizeof(rh_str), snap->rh, 1, "--");
        snprintf(str, sizeof(str), "RH:%s %%", rh_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: Pressure with hPa and 1 decimal */
    if (page == 6) {
        char press_str[12];
        float pressure_hpa = snap->pressure_pa / 100.0f;  /* Convert Pa to hPa */
        fmt_float(press_str, sizeof(press_str), pressure_hpa, 1, "----");
        snprintf(str, sizeof(str), "P:%s hPa", press_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: Sensor status with progress bar during warming */
    if (page == 7) {
        /* Draw progress bar with status text overlay */
        display_gfx_draw_progress_bar(buf, 0, DISPLAY_PAGE_WIDTH,
                                      snap->warmup_progress, snap->sensor_status, &s_font_label);
    }
}

void render_environment(uint8_t page, uint8_t *buf, const display_snapshot_t *snap)
{
    char str[32];
    float pressure_hpa = snap->pressure_pa / 100.0f;  /* Convert Pa to hPa */

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "Environment", &s_font_label);
    }

    /* Page 1-2: Temperature (8×16) */
    if (page >= 1 && page <= 2) {
        char tmp[16];
        fmt_float(tmp, sizeof(tmp), snap->temp, 1, "---");
        snprintf(str, sizeof(str), "%s C", tmp);
        display_gfx_draw_text_8x16_page(page, buf, 0, 8, str, &s_font_large);
    }

    /* Page 3: RH + Dewpoint */
    if (page == 3) {
        char rh_str[12], dew_str[12];
        fmt_float(rh_str, sizeof(rh_str), snap->rh, 1, "--");
        fmt_float(dew_str, sizeof(dew_str), snap->dewpt, 1, "--");
        snprintf(str, sizeof(str), "RH:%s%% Dew:%s", rh_str, dew_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: Pressure + trend icon */
    if (page == 4) {
        char p_str[12];
        fmt_float(p_str, sizeof(p_str), pressure_hpa, 0, "----");
        snprintf(str, sizeof(str), "P:%s hPa", p_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
        display_draw_icon_at(page, buf, 100, 32, get_pressure_trend_icon(snap->trend), false);
    }

    /* Page 5: Comfort */
    if (page == 5) {
        snprintf(str, sizeof(str), "Comfort:%d %s", snap->comfort, snap->comfort_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: Mold risk */
    if (page == 6) {
        snprintf(str, sizeof(str), "Mold:%d %s", snap->mold, snap->mold_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }
}

void render_air_quality(uint8_t page, uint8_t *buf, const display_snapshot_t *snap)
{
    char str[32];

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "Air Quality", &s_font_label);
    }

    /* Page 1-2: AQI (8×16) */
    if (page >= 1 && page <= 2) {
        snprintf(str, sizeof(str), "AQI:%u", snap->aqi);
        display_gfx_draw_text_8x16_page(page, buf, 0, 8, str, &s_font_large);
    }

    /* Page 3: AQI category */
    if (page == 3) {
        snprintf(str, sizeof(str), "%s", snap->aqi_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: PM2.5 bar (0-50 scale) with label */
    if (page == 4) {
        const int label_x = 0;
        const int bar_x = BAR_LABEL_WIDTH_PX;
        const int bar_w_max = DISPLAY_PAGE_WIDTH - bar_x;

        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), snap->pm25, 0, "--");
        snprintf(str, sizeof(str), "PM2.5:%s", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, label_x, 32, str, &s_font_label);

        int bar_width = (int)(snap->pm25 * (float)bar_w_max / 50.0f);
        if (bar_width > bar_w_max) bar_width = bar_w_max;
        if (bar_width < 0) bar_width = 0;
        display_gfx_draw_hbar(buf, bar_x, bar_width, 0xFF);
    }

    /* Page 5: PM10 bar (0-100 scale) with label */
    if (page == 5) {
        const int label_x = 0;
        const int bar_x = BAR_LABEL_WIDTH_PX;
        const int bar_w_max = DISPLAY_PAGE_WIDTH - bar_x;

        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), snap->pm10, 0, "--");
        snprintf(str, sizeof(str), "PM10:%s", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, label_x, 40, str, &s_font_label);

        int bar_width = (int)(snap->pm10 * (float)bar_w_max / 100.0f);
        if (bar_width > bar_w_max) bar_width = bar_w_max;
        if (bar_width < 0) bar_width = 0;
        display_gfx_draw_hbar(buf, bar_x, bar_width, 0xFF);
    }

    /* Page 6: VOC + NOx */
    if (page == 6) {
        snprintf(str, sizeof(str), "VOC:%s NOx:%s", snap->voc_cat, snap->nox_cat);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: IAQ score */
    if (page == 7) {
        snprintf(str, sizeof(str), "IAQ:%d/100", snap->iaq_score);
        display_gfx_draw_text_8x8_page(page, buf, 0, 56, str, &s_font_label);
    }
}

void render_co2_detail(uint8_t page, uint8_t *buf, const display_snapshot_t *snap)
{
    char str[32];

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "CO2 Detail", &s_font_label);
    }

    /* Page 1-3: Large CO2 (8×16, spans 2 pages) */
    if (page >= 1 && page <= 3) {
        char tmp[16];
        fmt_float(tmp, sizeof(tmp), snap->co2, 0, "---");
        snprintf(str, sizeof(str), "%s ppm", tmp);
        display_gfx_draw_text_8x16_page(page, buf, 0, 8, str, &s_font_large);
    }

    /* Page 4: Rate */
    if (page == 4) {
        char rate_str[12];
        fmt_float(rate_str, sizeof(rate_str), snap->co2_rate, 0, "---");
        snprintf(str, sizeof(str), "Rate:%s ppm/h", rate_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: Score */
    if (page == 5) {
        snprintf(str, sizeof(str), "Score:%d/100", snap->co2_score);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: ABC info */
    if (page == 6) {
        snprintf(str, sizeof(str), "ABC:%u (%u%%)", snap->abc_baseline, snap->abc_conf);
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: S8 status */
    if (page == 7) {
        snprintf(str, sizeof(str), "S8:%s", snap->s8_valid ? "OK" : "N/A");
        display_gfx_draw_text_8x8_page(page, buf, 0, 56, str, &s_font_label);
    }
}

void render_particulate(uint8_t page, uint8_t *buf, const display_snapshot_t *snap)
{
    char str[32];

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "Particulate", &s_font_label);
        if (snap->spike) {
            display_draw_icon_at(page, buf, 112, 0, ICON_ALERT, false);
        }
    }

    /* Page 1: PM1.0 */
    if (page == 1) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), snap->pm1, 0, "---");
        snprintf(str, sizeof(str), "PM1.0: %s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 8, str, &s_font_label);
    }

    /* Page 2: PM2.5 */
    if (page == 2) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), snap->pm25, 0, "---");
        snprintf(str, sizeof(str), "PM2.5: %s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 16, str, &s_font_label);
    }

    /* Page 3: PM10 */
    if (page == 3) {
        char pm_str[12];
        fmt_float(pm_str, sizeof(pm_str), snap->pm10, 0, "---");
        snprintf(str, sizeof(str), "PM10:  %s ug/m3", pm_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 24, str, &s_font_label);
    }

    /* Page 4: Quality */
    if (page == 4) {
        snprintf(str, sizeof(str), "Quality: %d%%", snap->pm_quality);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: PM1/PM2.5 ratio */
    if (page == 5) {
        char ratio_str[12];
        fmt_float(ratio_str, sizeof(ratio_str), snap->pm1_pm25_ratio, 2, "---");
        snprintf(str, sizeof(str), "PM1/PM2.5: %s", ratio_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }
}

void render_system(uint8_t page, uint8_t *buf, const display_snapshot_t *snap)
{
    char str[64];

    /* Page 0: Header */
    if (page == 0) {
        display_gfx_draw_text_8x8_page(page, buf, 0, 0, "System", &s_font_label);
    }

    /* Page 1: WiFi */
    if (page == 1) {
        display_draw_icon_at(page, buf, 0, 8, snap->wifi ? ICON_WIFI : ICON_WIFI_OFF, false);
        if (snap->wifi) {
            snprintf(str, sizeof(str), "RSSI:%ld dBm", (long)snap->rssi);
        } else {
            snprintf(str, sizeof(str), "Down");
        }
        display_gfx_draw_text_8x8_page(page, buf, 16, 8, str, &s_font_label);
    }

    /* Page 2: MQTT */
    if (page == 2) {
        display_draw_icon_at(page, buf, 0, 16, snap->mqtt ? ICON_MQTT : ICON_MQTT_OFF, false);
        snprintf(str, sizeof(str), "%s", snap->mqtt ? "Connected" : "Down");
        display_gfx_draw_text_8x8_page(page, buf, 16, 16, str, &s_font_label);
    }

    /* Page 3: Time */
    if (page == 3) {
        display_draw_icon_at(page, buf, 0, 24, ICON_CLOCK, false);
        if (snap->time_synced) {
            snprintf(str, sizeof(str), "%02d:%02d:%02d", snap->hour, snap->min, snap->sec);
        } else {
            snprintf(str, sizeof(str), "No sync");
        }
        display_gfx_draw_text_8x8_page(page, buf, 16, 24, str, &s_font_label);
    }

    /* Page 4: Uptime */
    if (page == 4) {
        char uptime_str[24];
        fmt_uptime(uptime_str, sizeof(uptime_str), snap->uptime);
        snprintf(str, sizeof(str), "Up: %s", uptime_str);
        display_gfx_draw_text_8x8_page(page, buf, 0, 32, str, &s_font_label);
    }

    /* Page 5: Internal RAM */
    if (page == 5) {
        snprintf(str, sizeof(str), "IRAM: %lu kB", (unsigned long)(snap->internal_free / 1024));
        display_gfx_draw_text_8x8_page(page, buf, 0, 40, str, &s_font_label);
    }

    /* Page 6: PSRAM */
    if (page == 6) {
        if (snap->spiram_total > 0) {
            snprintf(str, sizeof(str), "PSRAM: %lu kB", (unsigned long)(snap->spiram_free / 1024));
        } else {
            snprintf(str, sizeof(str), "PSRAM: N/A");
        }
        display_gfx_draw_text_8x8_page(page, buf, 0, 48, str, &s_font_label);
    }

    /* Page 7: Sensor status (ready/total count from snapshot) */
    if (page == 7) {
        snprintf(str, sizeof(str), "Status: %s", snap->sensor_status);
        display_gfx_draw_text_8x8_page(page, buf, 0, 56, str, &s_font_label);
    }
}

/* ===== Screen Table ===== */

static const screen_def_t s_screens[] = {
    { render_overview,     "Overview",    0 },
    { render_environment,  "Environment", 0 },
    { render_air_quality,  "Air Quality", 0 },
    { render_co2_detail,   "CO2",         0 },
    { render_particulate,  "PM",          0 },
    { render_system,       "System",      1000 },
};

#define NUM_SCREENS (sizeof(s_screens) / sizeof(s_screens[0]))

const screen_def_t* display_screens_get_table(void)
{
    return s_screens;
}

size_t display_screens_get_count(void)
{
    return NUM_SCREENS;
}

const display_font_t* display_screens_get_font_large(void)
{
    return &s_font_large;
}

const display_font_t* display_screens_get_font_label(void)
{
    return &s_font_label;
}

#else /* CONFIG_IAQ_OLED_ENABLE */

/* Stubs when OLED disabled */
void render_overview(uint8_t page, uint8_t *buf, const display_snapshot_t *snap) { (void)page; (void)buf; (void)snap; }
void render_environment(uint8_t page, uint8_t *buf, const display_snapshot_t *snap) { (void)page; (void)buf; (void)snap; }
void render_air_quality(uint8_t page, uint8_t *buf, const display_snapshot_t *snap) { (void)page; (void)buf; (void)snap; }
void render_co2_detail(uint8_t page, uint8_t *buf, const display_snapshot_t *snap) { (void)page; (void)buf; (void)snap; }
void render_particulate(uint8_t page, uint8_t *buf, const display_snapshot_t *snap) { (void)page; (void)buf; (void)snap; }
void render_system(uint8_t page, uint8_t *buf, const display_snapshot_t *snap) { (void)page; (void)buf; (void)snap; }

const screen_def_t* display_screens_get_table(void) { return NULL; }
size_t display_screens_get_count(void) { return 0; }
const display_font_t* display_screens_get_font_large(void) { return NULL; }
const display_font_t* display_screens_get_font_label(void) { return NULL; }

#endif /* CONFIG_IAQ_OLED_ENABLE */
