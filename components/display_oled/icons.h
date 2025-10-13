/* components/display_oled/icons.h */
#ifndef DISPLAY_ICONS_H
#define DISPLAY_ICONS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 8×8 icon bitmaps for OLED display.
 * Format: 8 bytes, column-major, LSB = top pixel
 * Compatible with SH1106 page buffer format.
 */

/* WiFi signal strength (connected) */
static const uint8_t ICON_WIFI[8] = {
    0x00, 0x00, 0x7E, 0x81, 0x3C, 0x42, 0x18, 0x18
};

/* WiFi disconnected (X through signal) */
static const uint8_t ICON_WIFI_OFF[8] = {
    0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81
};

/* MQTT broker online */
static const uint8_t ICON_MQTT[8] = {
    0x00, 0x08, 0x14, 0x22, 0x22, 0x14, 0x08, 0x00
};

/* MQTT broker offline (empty) */
static const uint8_t ICON_MQTT_OFF[8] = {
    0x00, 0x08, 0x14, 0x22, 0x22, 0x14, 0x08, 0x00
};

/* Clock (time synced) */
static const uint8_t ICON_CLOCK[8] = {
    0x3C, 0x42, 0x81, 0x85, 0x89, 0x81, 0x42, 0x3C
};

/* Alert/warning triangle */
static const uint8_t ICON_ALERT[8] = {
    0x08, 0x08, 0x14, 0x14, 0x22, 0x22, 0x7F, 0x00
};

/* Trend arrow: up */
static const uint8_t ICON_ARROW_UP[8] = {
    0x08, 0x1C, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x00
};

/* Trend arrow: down */
static const uint8_t ICON_ARROW_DOWN[8] = {
    0x08, 0x08, 0x08, 0x08, 0x3E, 0x1C, 0x08, 0x00
};

/* Trend arrow: stable (horizontal) */
static const uint8_t ICON_ARROW_STABLE[8] = {
    0x00, 0x08, 0x04, 0xFF, 0xFF, 0x04, 0x08, 0x00
};

/* Thermometer */
static const uint8_t ICON_TEMP[8] = {
    0x1C, 0x22, 0x22, 0x22, 0x22, 0x77, 0x77, 0x3E
};

/* Water droplet (humidity) */
static const uint8_t ICON_HUMIDITY[8] = {
    0x08, 0x1C, 0x1C, 0x3E, 0x3E, 0x7F, 0x7F, 0x3E
};

/**
 * Helper function: draw icon at position (x, y_px) if page matches.
 * Icons are 8×8, so they fit within one page.
 *
 * @param page Current page being rendered (0-7)
 * @param page_buf Page buffer (128 bytes)
 * @param x_px X position in pixels (0-127)
 * @param y_px Y position in pixels (0-63), will be converted to page
 * @param icon Pointer to 8-byte icon data
 * @param invert If true, invert the icon bits (for dark/light themes)
 */
static inline void display_draw_icon_at(uint8_t page, uint8_t *page_buf,
                                        int x_px, int y_px,
                                        const uint8_t *icon, bool invert)
{
    if ((y_px / 8) != page) return;
    if (x_px < 0 || x_px > 120) return;

    for (int col = 0; col < 8; ++col) {
        int x = x_px + col;
        if ((unsigned)x < 128U) {
            uint8_t byte = icon[col];
            if (invert) byte = ~byte;
            page_buf[x] |= byte;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_ICONS_H */
