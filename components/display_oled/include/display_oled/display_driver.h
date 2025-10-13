/* components/display_oled/include/display_oled/display_driver.h */
#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal SH1106 driver using shared I2C bus. */

/** Initialize SH1106 device and basic settings. Safe to call multiple times. */
esp_err_t display_driver_init(void);

/** Power control. Turns the panel on/off (does not deinit the I2C device). */
esp_err_t display_driver_power(bool on);

/** Set display contrast (0-255). */
esp_err_t display_driver_set_contrast(uint8_t contrast);

/** Set inverse mode (true=invert, false=normal). */
esp_err_t display_driver_set_invert(bool invert);

/** Set rotation (0 or 180 degrees). */
esp_err_t display_driver_set_rotation(int degrees);

/** Write one 128-byte page to the display at the given page index (0..7). */
esp_err_t display_driver_write_page(uint8_t page, const uint8_t *data128);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_DRIVER_H */

