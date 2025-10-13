/* components/display_oled/display_graphics.c */
#include "display_oled/display_graphics.h"
#include <string.h>
#include <stdbool.h>

/* Blit an 8x8 tile (u8x8 format: 8 bytes, one per column, LSB at top) at x_px */
void display_gfx_blit_tile(uint8_t *page_buf, int x_px, const uint8_t tile[8])
{
    if (x_px < 0 || x_px > 120) return; /* leave 8px room */
    for (int col = 0; col < 8; ++col) {
        int x = x_px + col;
        if ((unsigned)x < 128U) {
            /* OR blending for text */
            page_buf[x] |= tile[col];
        }
    }
}

/* internal: extract one 8x8 tile for ASCII code from u8x8 font */
static void font_get_tile(const uint8_t *font, unsigned char ch, uint8_t tile[8])
{
    uint8_t first = font[0];
    uint8_t last  = font[1];
    uint8_t th    = font[2];
    uint8_t tv    = font[3];
    (void)th; (void)tv; /* only support th=1,tv=1 for now */
    if (ch < first || ch > last) {
        memset(tile, 0, 8);
        return;
    }
    uint16_t tiles = (uint16_t)th * (uint16_t)tv;
    uint16_t idx = (uint16_t)(ch - first) * tiles; /* new 2019 format */
    uint16_t offset = 4 + (idx * 8);
    for (int i = 0; i < 8; ++i) {
        tile[i] = font[offset + i];
    }
}

void display_gfx_draw_text_8x8_page(uint8_t page, uint8_t *page_buf,
                                    int x_px, int y_px,
                                    const char *text,
                                    const display_font_t *font)
{
    if (!text || !font || !font->u8x8_font) return;
    /* Only draw if the baseline row maps to this page */
    if ((y_px / 8) != page) return;
    int x = x_px;
    uint8_t tile[8];
    for (const char *p = text; *p; ++p) {
        font_get_tile(font->u8x8_font, (unsigned char)*p, tile);
        display_gfx_blit_tile(page_buf, x, tile);
        x += 8; /* fixed width */
        if (x >= 128) break;
    }
}

/* Expand 8-bit vertical column to 16-bit by duplicating bits (vertical scale 2x). */
static inline uint16_t expand_vert_2x(uint8_t b)
{
    uint16_t y = b;
    y = (uint16_t)((y | (y << 4)) & 0x0F0F);
    y = (uint16_t)((y | (y << 2)) & 0x3333);
    y = (uint16_t)((y | (y << 1)) & 0x5555);
    y |= (uint16_t)(y << 1);
    return y;
}

void display_gfx_draw_text_8x16_page(uint8_t page, uint8_t *page_buf,
                                     int x_px, int y_px,
                                     const char *text,
                                     const display_font_t *font)
{
    if (!text || !font || !font->u8x8_font) return;
    /* Top page index for 8x16 text */
    uint8_t top_page = (uint8_t)(y_px / 8);
    uint8_t bot_page = (uint8_t)(top_page + 1);
    if (!(page == top_page || page == bot_page)) return;

    int x = x_px;
    uint8_t src[8];
    for (const char *p = text; *p; ++p) {
        font_get_tile(font->u8x8_font, (unsigned char)*p, src);
        /* Expand each column into two 8-bit slices */
        uint8_t upper[8];
        uint8_t lower[8];
        for (int i = 0; i < 8; ++i) {
            uint16_t t = expand_vert_2x(src[i]);
            lower[i] = (uint8_t)(t & 0xFF);
            upper[i] = (uint8_t)(t >> 8);
        }
        if (page == top_page) {
            display_gfx_blit_tile(page_buf, x, lower);
        } else {
            display_gfx_blit_tile(page_buf, x, upper);
        }
        x += 8;
        if (x >= 128) break;
    }
}

void display_gfx_draw_hbar(uint8_t *page_buf, int x_px, int width, uint8_t mask)
{
    if (!page_buf || width <= 0) return;

    for (int i = 0; i < width && (x_px + i) < 128; i++) {
        int x = x_px + i;
        if (x >= 0) {
            page_buf[x] |= mask;
        }
    }

    /* Add tick marks every 16 pixels for scale reference (clear bit 3) */
    for (int tick = 0; tick * 16 < width; tick++) {
        int tx = x_px + tick * 16;
        if (tx >= 0 && tx < 128) {
            page_buf[tx] &= ~(1 << 3);
        }
    }
}

void display_gfx_draw_icon(uint8_t *page_buf, int x_px, const uint8_t icon[8], bool invert)
{
    if (!page_buf || !icon) return;
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

void display_gfx_draw_hline(uint8_t *page_buf, int x_px, int width, bool top)
{
    if (!page_buf || width <= 0) return;

    uint8_t mask = top ? 0x01 : 0x80;

    for (int i = 0; i < width && (x_px + i) < 128; i++) {
        int x = x_px + i;
        if (x >= 0) {
            page_buf[x] |= mask;
        }
    }
}

uint16_t display_gfx_page_hash(const uint8_t *page_buf)
{
    if (!page_buf) return 0;

    /* Simple 16-bit rolling hash (FNV-1a style) */
    uint16_t hash = 0x811C;

    for (int i = 0; i < 128; i++) {
        hash ^= page_buf[i];
        hash *= 0x0101;
        hash ^= (hash >> 8);
    }

    return hash;
}

void display_gfx_draw_progress_bar(uint8_t *page_buf, int x_px, int width,
                                     uint8_t percentage, const char *text,
                                     const display_font_t *font)
{
    if (!page_buf || width <= 0 || percentage > 100) return;

    /* Calculate filled width based on percentage */
    int filled_width = (width * percentage) / 100;
    if (filled_width < 0) filled_width = 0;
    if (filled_width > width) filled_width = width;

    /* Fill the progress bar (filled portion only, no borders) */
    for (int i = 0; i < filled_width && (x_px + i) < 128; i++) {
        int x = x_px + i;
        if (x >= 0) {
            page_buf[x] |= 0xFF;  /* Fill entire 8-pixel height */
        }
    }

    /* Draw text overlay with XOR blending for inversion */
    if (text && font && font->u8x8_font) {
        uint8_t first = font->u8x8_font[0];
        uint8_t last  = font->u8x8_font[1];
        uint8_t th    = font->u8x8_font[2];
        uint8_t tv    = font->u8x8_font[3];
        (void)th; (void)tv; /* only support th=1,tv=1 */

        /* Calculate text width and center it */
        int text_len = 0;
        for (const char *p = text; *p; ++p) text_len++;
        int text_width = text_len * 8;  /* 8 pixels per character */
        int text_x = x_px + (width - text_width) / 2;

        uint8_t tile[8];

        for (const char *p = text; *p; ++p) {
            unsigned char ch = (unsigned char)*p;
            if (ch < first || ch > last) {
                memset(tile, 0, 8);
            } else {
                uint16_t tiles = (uint16_t)th * (uint16_t)tv;
                uint16_t idx = (uint16_t)(ch - first) * tiles;
                uint16_t offset = 4 + (idx * 8);
                for (int i = 0; i < 8; ++i) {
                    tile[i] = font->u8x8_font[offset + i];
                }
            }

            /* XOR blit for text inversion over filled area */
            for (int col = 0; col < 8; ++col) {
                int x = text_x + col;
                if (x >= 0 && x < 128) {
                    page_buf[x] ^= tile[col];
                }
            }

            text_x += 8;
            if (text_x >= 128) break;
        }
    }
}

