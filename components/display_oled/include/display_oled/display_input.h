/* components/display_oled/include/display_oled/display_input.h */
#ifndef DISPLAY_INPUT_H
#define DISPLAY_INPUT_H

#include <stdbool.h>
#include "esp_err.h"

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

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_INPUT_H */

