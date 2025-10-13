/* components/display_oled/include/display_oled/display_graphics.h */
#ifndef DISPLAY_GRAPHICS_H
#define DISPLAY_GRAPHICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal page-based graphics helpers for 128x64 SH1106.
 * - Page buffer is 128 bytes (one 8-pixel-high page)
 * - Text rendering uses u8x8 tile fonts (8x8 per glyph) from u8g2
 */

typedef struct {
    const uint8_t *u8x8_font;  /* Pointer to u8x8 font array (2019 format) */
} display_font_t;

/* Clear a page buffer (fill with 0). */
static inline void display_gfx_clear(uint8_t *page_buf) {
    for (int i = 0; i < 128; ++i) page_buf[i] = 0;
}

/* Draw a raw 8x8 tile at pixel X (0..127) into the current page buffer. */
void display_gfx_blit_tile(uint8_t *page_buf, int x_px, const uint8_t tile[8]);

/* Draw ASCII string using a u8x8 font. Only the portion that maps to the given page is drawn. */
void display_gfx_draw_text_8x8_page(uint8_t page, uint8_t *page_buf,
                                    int x_px, int y_px,
                                    const char *text,
                                    const display_font_t *font);

/* Draw ASCII string scaled 1x2 (8x16) using the same 8x8 font. Renders across two pages if needed. */
void display_gfx_draw_text_8x16_page(uint8_t page, uint8_t *page_buf,
                                     int x_px, int y_px,
                                     const char *text,
                                     const display_font_t *font);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_GRAPHICS_H */

