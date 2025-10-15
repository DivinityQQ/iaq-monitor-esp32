/* components/display_oled/include/display_oled/display_input.h */
#ifndef DISPLAY_INPUT_H
#define DISPLAY_INPUT_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_BTN_EVENT_NONE = 0,
    DISPLAY_BTN_EVENT_SHORT,
    DISPLAY_BTN_EVENT_LONG,
} display_button_event_t;

/* Initialize button input (GPIO/ISR) if configured. Safe to call even if disabled. */
esp_err_t display_input_init(void);

/* Poll for a debounced event; returns DISPLAY_BTN_EVENT_NONE if no event. */
display_button_event_t display_input_poll_event(void);

/* Optional: register a task to notify on button events.
 * The ISR will set bits on the target task's notify value using eSetBits.
 * Pass 0 for a bit to disable notifications for that event. */
void display_input_set_notify_task(TaskHandle_t task, uint32_t short_press_bit, uint32_t long_press_bit);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_INPUT_H */
