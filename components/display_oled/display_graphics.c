/* components/display_oled/display_graphics.c */
#include "display_oled/display_graphics.h"
#include <string.h>

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

