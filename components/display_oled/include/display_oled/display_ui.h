/* components/display_oled/include/display_oled/display_ui.h */
#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "system_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the OLED UI module and register event handlers. */
esp_err_t display_ui_init(iaq_system_context_t *ctx);

/* Start the display task (creates task if enabled in Kconfig). */
esp_err_t display_ui_start(void);

/* Turn display on/off (overrides auto-off). */
void display_ui_set_enabled(bool on);
bool display_ui_is_enabled(void);

/* Advance to next/previous screen (wraps around). */
void display_ui_next_screen(void);
void display_ui_prev_screen(void);

/* Wake display for specified seconds (used during night mode). */
void display_ui_wake_for_seconds(uint32_t seconds);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UI_H */

