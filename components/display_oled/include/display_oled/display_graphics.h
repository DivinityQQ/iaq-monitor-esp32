/* components/display_oled/include/display_oled/display_graphics.h */
#ifndef DISPLAY_GRAPHICS_H
#define DISPLAY_GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal page-based graphics helpers for 128x64 SH1106.
 * - Page buffer is 128 bytes (one 8-pixel-high page)
 * - Text rendering uses u8x8 tile fonts (8x8 per glyph) from u8g2
 */

#define DISPLAY_PAGE_WIDTH  128

typedef struct {
    const uint8_t *u8x8_font;  /* Pointer to u8x8 font array (2019 format) */
} display_font_t;

/* Clear a page buffer (fill with 0). */
static inline void display_gfx_clear(uint8_t *page_buf) {
    memset(page_buf, 0, DISPLAY_PAGE_WIDTH);
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

/* Draw horizontal bar using byte-mask fill (optimized for page-aligned bars).
 * mask: 0xFF = full 8px height, 0x0F = bottom 4px, 0xF0 = top 4px, etc.
 * Adds tick marks every 16 pixels for scale reference. */
void display_gfx_draw_hbar(uint8_t *page_buf, int x_px, int width, uint8_t mask);

/* Draw 8x8 icon at x position with optional invert (reuse bitmaps for dark/light). */
void display_gfx_draw_icon(uint8_t *page_buf, int x_px, const uint8_t icon[8], bool invert);

/* Draw horizontal line (page-aligned top or bottom edge).
 * top=true: draw at top of page (bit 0), top=false: draw at bottom (bit 7) */
void display_gfx_draw_hline(uint8_t *page_buf, int x_px, int width, bool top);

/* Compute 16-bit rolling hash of page buffer for change detection. */
uint16_t display_gfx_page_hash(const uint8_t *page_buf);

/* Draw progress bar with optional text overlay (XOR blended for inversion).
 * percentage: 0-100, width: total bar width in pixels
 * Text is centered and inverted where it overlaps the filled portion. */
void display_gfx_draw_progress_bar(uint8_t *page_buf, int x_px, int width,
                                     uint8_t percentage, const char *text,
                                     const display_font_t *font);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_GRAPHICS_H */

