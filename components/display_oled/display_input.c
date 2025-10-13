/* components/display_oled/display_input.c */
#include "display_oled/display_input.h"
#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_IAQ_OLED_ENABLE

#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "OLED_IN";

static int64_t s_last_edge_us = 0;
static volatile display_button_event_t s_pending = DISPLAY_BTN_EVENT_NONE;

static void IRAM_ATTR gpio_isr(void *arg)
{
    (void)arg;
    int level = gpio_get_level(CONFIG_IAQ_OLED_BUTTON_GPIO);
#if CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW
    bool pressed = (level == 0);
#else
    bool pressed = (level != 0);
#endif
    int64_t now = esp_timer_get_time();
    if (pressed) {
        s_last_edge_us = now;
    } else {
        int64_t dt_ms = (now - s_last_edge_us) / 1000;
        if (dt_ms >= CONFIG_IAQ_OLED_BUTTON_DEBOUNCE_MS) {
            s_pending = (dt_ms >= CONFIG_IAQ_OLED_BUTTON_LONG_MS) ? DISPLAY_BTN_EVENT_LONG : DISPLAY_BTN_EVENT_SHORT;
        }
    }
}

esp_err_t display_input_init(void)
{
#if (CONFIG_IAQ_OLED_BUTTON_GPIO < 0)
    return ESP_OK;
#else
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << CONFIG_IAQ_OLED_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return gpio_isr_handler_add(CONFIG_IAQ_OLED_BUTTON_GPIO, gpio_isr, NULL);
#endif
}

display_button_event_t display_input_poll_event(void)
{
    display_button_event_t ev = s_pending;
    s_pending = DISPLAY_BTN_EVENT_NONE;
    return ev;
}

#else /* CONFIG_IAQ_OLED_ENABLE */

esp_err_t display_input_init(void) { return ESP_OK; }
display_button_event_t display_input_poll_event(void) { return DISPLAY_BTN_EVENT_NONE; }

#endif /* CONFIG_IAQ_OLED_ENABLE */

