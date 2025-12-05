/* components/display_oled/include/display_oled/display_screens.h */
#ifndef DISPLAY_SCREENS_H
#define DISPLAY_SCREENS_H

#include <stdint.h>
#include <stdbool.h>
#include "display_oled/display_graphics.h"
#include "iaq_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Snapshot of display-relevant data, copied under a single lock.
 * Passed to screen render functions to avoid repeated locking.
 */
typedef struct display_snapshot {
    /* Sensor readings */
    float co2;
    float temp;
    float rh;
    float pm25;
    float pm10;
    float pm1;
    float pressure_pa;
    float dewpt;
    float co2_rate;
    float pm1_pm25_ratio;

    /* Metrics */
    uint16_t aqi;
    int comfort;
    int mold;
    int co2_score;
    int iaq_score;
    int pm_quality;

    /* Diagnostic data */
    uint16_t abc_baseline;
    uint8_t abc_conf;
    bool s8_valid;
    bool spike;

    /* System status */
    bool wifi;
    bool mqtt;
    bool time_synced;
    int32_t rssi;
    uint32_t uptime;
    uint32_t heap;
    uint32_t min_heap;

    /* Time (if synced) */
    int hour;
    int min;
    int sec;

    /* Category strings (pointers to static strings in iaq_data) */
    const char *aqi_cat;
    const char *comfort_cat;
    const char *mold_cat;
    const char *voc_cat;
    const char *nox_cat;

    /* Pressure trend */
    pressure_trend_t trend;

    /* Sensor warmup status */
    bool warming;
    uint8_t warmup_progress;
    const char *sensor_status;
} display_snapshot_t;

/**
 * Screen render function type.
 * Renders one page (0-7) of an 8-page 128x64 display.
 *
 * @param page Page index (0-7)
 * @param buf 128-byte page buffer
 * @param snap Pre-fetched display data snapshot
 */
typedef void (*screen_render_fn_t)(uint8_t page, uint8_t *buf,
                                   const display_snapshot_t *snap);

/**
 * Screen definition structure.
 */
typedef struct {
    screen_render_fn_t render;
    const char *name;
    uint16_t refresh_ms;  /* 0 = use global default */
} screen_def_t;

/* Screen renderer functions */
void render_overview(uint8_t page, uint8_t *buf, const display_snapshot_t *snap);
void render_environment(uint8_t page, uint8_t *buf, const display_snapshot_t *snap);
void render_air_quality(uint8_t page, uint8_t *buf, const display_snapshot_t *snap);
void render_co2_detail(uint8_t page, uint8_t *buf, const display_snapshot_t *snap);
void render_particulate(uint8_t page, uint8_t *buf, const display_snapshot_t *snap);
void render_system(uint8_t page, uint8_t *buf, const display_snapshot_t *snap);

/* Screen table access */
const screen_def_t* display_screens_get_table(void);
size_t display_screens_get_count(void);

/* Fonts for screen rendering */
const display_font_t* display_screens_get_font_large(void);
const display_font_t* display_screens_get_font_label(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_SCREENS_H */
