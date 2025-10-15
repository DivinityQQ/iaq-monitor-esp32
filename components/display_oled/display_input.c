/* components/display_oled/display_input.c */
#include "display_oled/display_input.h"
#include "sdkconfig.h"
#include "esp_log.h"

/* Only compile real button support if OLED is enabled and a valid GPIO is set */
#if CONFIG_IAQ_OLED_ENABLE && (CONFIG_IAQ_OLED_BUTTON_GPIO >= 0)

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OLED_IN";

static volatile TickType_t s_last_edge_ticks = 0;
static volatile display_button_event_t s_pending = DISPLAY_BTN_EVENT_NONE;
static TaskHandle_t s_notify_task = NULL;
static uint32_t s_notify_bit_short = 0;
static uint32_t s_notify_bit_long = 0;

static void IRAM_ATTR gpio_isr(void *arg)
{
    (void)arg;
    int level = gpio_get_level(CONFIG_IAQ_OLED_BUTTON_GPIO);
#if CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW
    bool pressed = (level == 0);
#else
    bool pressed = (level != 0);
#endif
    TickType_t now_ticks = xTaskGetTickCountFromISR();
    if (pressed) {
        s_last_edge_ticks = now_ticks;
    } else {
        TickType_t dt_ticks = now_ticks - s_last_edge_ticks;
        int32_t dt_ms = (int32_t)(dt_ticks * portTICK_PERIOD_MS);
        if (dt_ms >= CONFIG_IAQ_OLED_BUTTON_DEBOUNCE_MS) {
            display_button_event_t ev = (dt_ms >= CONFIG_IAQ_OLED_BUTTON_LONG_MS) ? DISPLAY_BTN_EVENT_LONG : DISPLAY_BTN_EVENT_SHORT;
            s_pending = ev;
            /* Notify target task if registered */
            if (s_notify_task) {
                BaseType_t hpwoken = pdFALSE;
                uint32_t bits = (ev == DISPLAY_BTN_EVENT_SHORT) ? s_notify_bit_short : s_notify_bit_long;
                if (bits) {
                    (void)xTaskNotifyFromISR(s_notify_task, bits, eSetBits, &hpwoken);
                    if (hpwoken == pdTRUE) {
                        portYIELD_FROM_ISR();
                    }
                }
            }
        }
    }
}

esp_err_t display_input_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << CONFIG_IAQ_OLED_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIO%d: %s", CONFIG_IAQ_OLED_BUTTON_GPIO, esp_err_to_name(err));
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = gpio_isr_handler_add(CONFIG_IAQ_OLED_BUTTON_GPIO, gpio_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler for GPIO%d: %s", CONFIG_IAQ_OLED_BUTTON_GPIO, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Button input initialized (GPIO%d, %s)",
             CONFIG_IAQ_OLED_BUTTON_GPIO,
             CONFIG_IAQ_OLED_BUTTON_ACTIVE_LOW ? "active-low" : "active-high");
    return ESP_OK;
}

display_button_event_t display_input_poll_event(void)
{
    display_button_event_t ev = s_pending;
    s_pending = DISPLAY_BTN_EVENT_NONE;
    return ev;
}

void display_input_set_notify_task(TaskHandle_t task, uint32_t short_press_bit, uint32_t long_press_bit)
{
    s_notify_task = task;
    s_notify_bit_short = short_press_bit;
    s_notify_bit_long = long_press_bit;
}

#else /* stubs if OLED disabled or button GPIO < 0 */

esp_err_t display_input_init(void) { return ESP_OK; }
display_button_event_t display_input_poll_event(void) { return DISPLAY_BTN_EVENT_NONE; }
void display_input_set_notify_task(TaskHandle_t task, uint32_t short_press_bit, uint32_t long_press_bit)
{ (void)task; (void)short_press_bit; (void)long_press_bit; }

#endif /* CONFIG_IAQ_OLED_ENABLE && (CONFIG_IAQ_OLED_BUTTON_GPIO >= 0) */
