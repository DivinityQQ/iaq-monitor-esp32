/* components/display_oled/display_ui.c */
#include "display_oled/display_ui.h"
#include "display_oled/display_driver.h"
#include "display_oled/display_graphics.h"
#include "display_oled/display_input.h"

#include "iaq_config.h"
#include "system_context.h"
#include "time_sync.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

/* Fonts (u8x8) */
#include "fonts/u8x8_font_chroma48medium8_r.h"

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

static iaq_system_context_t *s_ctx = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_enabled = true;
static volatile int s_screen_idx = 0;
static volatile int64_t s_last_activity_us = 0;
static display_font_t s_font_small = { .u8x8_font = u8x8_font_chroma48medium8_r };
static bool s_invert = false;

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

void display_ui_set_enabled(bool on)
{
    s_enabled = on;
    (void)display_driver_power(on);
    if (on) mark_activity();
}

bool display_ui_is_enabled(void) { return s_enabled; }

void display_ui_next_screen(void)
{
    s_screen_idx = (s_screen_idx + 1) % (int)(sizeof(s_screens)/sizeof(s_screens[0]));
    mark_activity();
}

void display_ui_prev_screen(void)
{
    s_screen_idx = (s_screen_idx - 1);
    if (s_screen_idx < 0) s_screen_idx = (int)(sizeof(s_screens)/sizeof(s_screens[0])) - 1;
    mark_activity();
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t page_buf[128];

    for (;;) {
        if (!s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Handle input events */
        display_button_event_t ev = display_input_poll_event();
        if (ev == DISPLAY_BTN_EVENT_SHORT) {
            display_ui_next_screen();
        } else if (ev == DISPLAY_BTN_EVENT_LONG) {
            s_invert = !s_invert;
            (void)display_driver_set_invert(s_invert);
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

        /* Night mode: keep display off unless woken by input */
        if (is_night_now()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Render all pages */
        for (uint8_t page = 0; page < 8; ++page) {
            display_gfx_clear(page_buf);
            s_screens[s_screen_idx].render(page, page_buf, true);
            (void)display_driver_write_page(page, page_buf);
        }

        /* Sleep until next refresh */
        vTaskDelay(pdMS_TO_TICKS(get_refresh_ms()));
    }
}

static void render_overview(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;
    display_gfx_draw_text_8x16_page(page, buf, 0, 0, "Overview", &s_font_small);
}

static void render_environment(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;
    display_gfx_draw_text_8x16_page(page, buf, 0, 0, "Environment", &s_font_small);
}

static void render_air_quality(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;
    display_gfx_draw_text_8x16_page(page, buf, 0, 0, "Air Quality", &s_font_small);
}

static void render_co2_detail(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;
    display_gfx_draw_text_8x16_page(page, buf, 0, 0, "CO2", &s_font_small);
}

static void render_particulate(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;
    display_gfx_draw_text_8x16_page(page, buf, 0, 0, "PM", &s_font_small);
}

static void render_system(uint8_t page, uint8_t *buf, bool full)
{
    (void)full;
    display_gfx_draw_text_8x16_page(page, buf, 0, 0, "System", &s_font_small);
}

static void iaq_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base != IAQ_EVENT) return;
    switch (id) {
        case IAQ_EVENT_WIFI_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            break;
        case IAQ_EVENT_WIFI_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi disconnected");
            break;
        case IAQ_EVENT_TIME_SYNCED:
            ESP_LOGI(TAG, "Time synced event");
            break;
        default: break;
    }
}

esp_err_t display_ui_init(iaq_system_context_t *ctx)
{
    s_ctx = ctx;
    if (!CONFIG_IAQ_OLED_ENABLE) {
        ESP_LOGI(TAG, "OLED disabled by Kconfig");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(display_driver_init(), TAG, "driver init failed");
    ESP_RETURN_ON_ERROR(display_input_init(), TAG, "input init failed");

    esp_err_t err = esp_event_handler_register(IAQ_EVENT, ESP_EVENT_ANY_ID, &iaq_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_enabled = true;
    s_last_activity_us = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t display_ui_start(void)
{
    if (!CONFIG_IAQ_OLED_ENABLE) return ESP_OK;
    if (s_task) return ESP_OK;
    BaseType_t ok = xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_DISPLAY,
                                            NULL, TASK_PRIORITY_DISPLAY, &s_task, TASK_CORE_DISPLAY);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Display task started (core %d)", TASK_CORE_DISPLAY);
    return ESP_OK;
}

#else /* CONFIG_IAQ_OLED_ENABLE */

esp_err_t display_ui_init(iaq_system_context_t *ctx) { (void)ctx; return ESP_OK; }
esp_err_t display_ui_start(void) { return ESP_OK; }
void display_ui_set_enabled(bool on) { (void)on; }
bool display_ui_is_enabled(void) { return false; }
void display_ui_next_screen(void) {}
void display_ui_prev_screen(void) {}

#endif /* CONFIG_IAQ_OLED_ENABLE */
