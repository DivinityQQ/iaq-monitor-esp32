/* components/display_oled/display_graphics.c */
#include "display_oled/display_graphics.h"
#include <string.h>
#include <stdbool.h>

/* Precomputed 8-bit to 16-bit vertical expansion table.
 * Each input bit expands to 2 output bits: 0->00, 1->11.
 * Used for 2x vertical scaling of 8x8 fonts to 8x16. */
static const uint16_t s_expand_2x[256] = {
    0x0000, 0x0003, 0x000C, 0x000F, 0x0030, 0x0033, 0x003C, 0x003F,
    0x00C0, 0x00C3, 0x00CC, 0x00CF, 0x00F0, 0x00F3, 0x00FC, 0x00FF,
    0x0300, 0x0303, 0x030C, 0x030F, 0x0330, 0x0333, 0x033C, 0x033F,
    0x03C0, 0x03C3, 0x03CC, 0x03CF, 0x03F0, 0x03F3, 0x03FC, 0x03FF,
    0x0C00, 0x0C03, 0x0C0C, 0x0C0F, 0x0C30, 0x0C33, 0x0C3C, 0x0C3F,
    0x0CC0, 0x0CC3, 0x0CCC, 0x0CCF, 0x0CF0, 0x0CF3, 0x0CFC, 0x0CFF,
    0x0F00, 0x0F03, 0x0F0C, 0x0F0F, 0x0F30, 0x0F33, 0x0F3C, 0x0F3F,
    0x0FC0, 0x0FC3, 0x0FCC, 0x0FCF, 0x0FF0, 0x0FF3, 0x0FFC, 0x0FFF,
    0x3000, 0x3003, 0x300C, 0x300F, 0x3030, 0x3033, 0x303C, 0x303F,
    0x30C0, 0x30C3, 0x30CC, 0x30CF, 0x30F0, 0x30F3, 0x30FC, 0x30FF,
    0x3300, 0x3303, 0x330C, 0x330F, 0x3330, 0x3333, 0x333C, 0x333F,
    0x33C0, 0x33C3, 0x33CC, 0x33CF, 0x33F0, 0x33F3, 0x33FC, 0x33FF,
    0x3C00, 0x3C03, 0x3C0C, 0x3C0F, 0x3C30, 0x3C33, 0x3C3C, 0x3C3F,
    0x3CC0, 0x3CC3, 0x3CCC, 0x3CCF, 0x3CF0, 0x3CF3, 0x3CFC, 0x3CFF,
    0x3F00, 0x3F03, 0x3F0C, 0x3F0F, 0x3F30, 0x3F33, 0x3F3C, 0x3F3F,
    0x3FC0, 0x3FC3, 0x3FCC, 0x3FCF, 0x3FF0, 0x3FF3, 0x3FFC, 0x3FFF,
    0xC000, 0xC003, 0xC00C, 0xC00F, 0xC030, 0xC033, 0xC03C, 0xC03F,
    0xC0C0, 0xC0C3, 0xC0CC, 0xC0CF, 0xC0F0, 0xC0F3, 0xC0FC, 0xC0FF,
    0xC300, 0xC303, 0xC30C, 0xC30F, 0xC330, 0xC333, 0xC33C, 0xC33F,
    0xC3C0, 0xC3C3, 0xC3CC, 0xC3CF, 0xC3F0, 0xC3F3, 0xC3FC, 0xC3FF,
    0xCC00, 0xCC03, 0xCC0C, 0xCC0F, 0xCC30, 0xCC33, 0xCC3C, 0xCC3F,
    0xCCC0, 0xCCC3, 0xCCCC, 0xCCCF, 0xCCF0, 0xCCF3, 0xCCFC, 0xCCFF,
    0xCF00, 0xCF03, 0xCF0C, 0xCF0F, 0xCF30, 0xCF33, 0xCF3C, 0xCF3F,
    0xCFC0, 0xCFC3, 0xCFCC, 0xCFCF, 0xCFF0, 0xCFF3, 0xCFFC, 0xCFFF,
    0xF000, 0xF003, 0xF00C, 0xF00F, 0xF030, 0xF033, 0xF03C, 0xF03F,
    0xF0C0, 0xF0C3, 0xF0CC, 0xF0CF, 0xF0F0, 0xF0F3, 0xF0FC, 0xF0FF,
    0xF300, 0xF303, 0xF30C, 0xF30F, 0xF330, 0xF333, 0xF33C, 0xF33F,
    0xF3C0, 0xF3C3, 0xF3CC, 0xF3CF, 0xF3F0, 0xF3F3, 0xF3FC, 0xF3FF,
    0xFC00, 0xFC03, 0xFC0C, 0xFC0F, 0xFC30, 0xFC33, 0xFC3C, 0xFC3F,
    0xFCC0, 0xFCC3, 0xFCCC, 0xFCCF, 0xFCF0, 0xFCF3, 0xFCFC, 0xFCFF,
    0xFF00, 0xFF03, 0xFF0C, 0xFF0F, 0xFF30, 0xFF33, 0xFF3C, 0xFF3F,
    0xFFC0, 0xFFC3, 0xFFCC, 0xFFCF, 0xFFF0, 0xFFF3, 0xFFFC, 0xFFFF,
};

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
    return s_expand_2x[b];
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

    /* FNV-1a 16-bit hash for page buffer change detection */
    uint16_t hash = 0x811C;  /* FNV offset basis (truncated) */

    for (int i = 0; i < DISPLAY_PAGE_WIDTH; i++) {
        hash ^= page_buf[i];
        hash *= 0x0193;  /* FNV prime for 16-bit */
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
        /* Calculate text width and center it */
        int text_len = (int)strlen(text);
        int text_width = text_len * 8;  /* 8 pixels per character */
        int text_x = x_px + (width - text_width) / 2;

        uint8_t tile[8];

        for (const char *p = text; *p; ++p) {
            font_get_tile(font->u8x8_font, (unsigned char)*p, tile);

            /* XOR blit for text inversion over filled area */
            for (int col = 0; col < 8; ++col) {
                int x = text_x + col;
                if (x >= 0 && x < DISPLAY_PAGE_WIDTH) {
                    page_buf[x] ^= tile[col];
                }
            }

            text_x += 8;
            if (text_x >= DISPLAY_PAGE_WIDTH) break;
        }
    }
}

