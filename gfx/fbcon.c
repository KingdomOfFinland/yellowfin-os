#include <stdint.h>

#define VBE_OK    (*(volatile uint8_t  *)0x8FF0)
#define VBE_PITCH (*(volatile uint16_t *)0x9010)
#define VBE_W     (*(volatile uint16_t *)0x9012)
#define VBE_H     (*(volatile uint16_t *)0x9014)
#define VBE_BPP   (*(volatile uint8_t  *)0x9019)
#define VBE_FB    (*(volatile uint32_t *)0x9028)
#define FONT      ((const uint8_t *)0x9400)

static const uint32_t pal[16] = {
    0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
    0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF };

static int fb_on;
static uint8_t *fb;
static uint32_t pitch, bpp, cols, rows, cx, cy;

int fbcon_on(void) { return fb_on; }

int fbcon_init(void)
{
    if (!VBE_OK) return 0;
    fb = (uint8_t *)VBE_FB;
    pitch = VBE_PITCH;
    bpp = VBE_BPP / 8;
    cols = VBE_W / 8;
    rows = VBE_H / 16;
    cx = cy = 0;
    fb_on = 1;
    return 1;
}

uint32_t fbcon_cols(void) { return cols; }
uint32_t fbcon_rows(void) { return rows; }

static inline void px(uint32_t x, uint32_t y, uint32_t c)
{
    uint8_t *p = fb + y * pitch + x * bpp;
    if (bpp == 4) *(uint32_t *)p = c;
    else { p[0] = c; p[1] = c >> 8; p[2] = c >> 16; }
}

/* Fill a rectangle with one colour (32bpp rows are written as dwords). */
static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    for (uint32_t yy = y; yy < y + h; yy++) {
        uint8_t *p = fb + yy * pitch + x * bpp;

        if (bpp == 4) {
            uint32_t n = w;
            __asm__ volatile ("cld; rep stosl"
                              : "+D"(p), "+c"(n) : "a"(c) : "memory");
        } else {
            for (uint32_t i = 0; i < w; i++, p += 3) {
                p[0] = c; p[1] = c >> 8; p[2] = c >> 16;
            }
        }
    }
}

/* Software cursor: inverts one 8x16 cell. Flipping twice restores it. */
static int cur_shown;
static uint32_t cur_col, cur_row;

static void cursor_flip(uint32_t col, uint32_t row)
{
    for (uint32_t y = 0; y < 16; y++) {
        uint8_t *p = fb + (row * 16 + y) * pitch + col * 8 * bpp;
        for (uint32_t i = 0; i < 8 * bpp; i++)
            p[i] ^= 0xFF;
    }
}

static void cursor_hide(void)
{
    if (cur_shown) {
        cursor_flip(cur_col, cur_row);
        cur_shown = 0;
    }
}

static void cursor_show(void)
{
    cursor_hide();
    if (fb_on && cx < cols && cy < rows) {
        cur_col = cx;
        cur_row = cy;
        cursor_flip(cur_col, cur_row);
        cur_shown = 1;
    }
}

static void glyph(char ch, uint32_t col, uint32_t row, uint32_t fg, uint32_t bg)
{
    const uint8_t *g = FONT + (uint8_t)ch * 16;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 8; x++)
            px(col * 8 + x, row * 16 + y, (g[y] & (0x80 >> x)) ? fg : bg);
}


static void scroll(void)
{
    if (!fb_on || rows == 0 || pitch == 0)
        return;

    cursor_hide();

    uint8_t *dst = fb;
    uint8_t *src = fb + 16 * pitch;
    uint32_t dwords = (rows - 1) * 16 * pitch / 4;   /* pitch is a multiple of 4 */

    __asm__ volatile ("cld; rep movsl"
                      : "+D"(dst), "+S"(src), "+c"(dwords) : : "memory");

    fill(0, (rows - 1) * 16, VBE_W, 16, 0);
    cy = rows - 1;
}

void fbcon_putc(char ch, uint8_t attr)
{
	cursor_hide();
    uint32_t fg = pal[attr & 15], bg = pal[(attr >> 4) & 15];
    if (ch == '\n')      { cx = 0; cy++; }
    else if (ch == '\r') { cx = 0; }
    else if (ch == '\b') { if (cx) { cx--; glyph(' ', cx, cy, fg, bg); } }
    else { glyph(ch, cx, cy, fg, bg); cx++; }
    if (cx >= cols) { cx = 0; cy++; }
    if (cy >= rows) scroll();
}

void fbcon_clear(uint8_t attr)
{
    if (!fb_on)
        return;

    cursor_hide();
    fill(0, 0, VBE_W, VBE_H, pal[(attr >> 4) & 15]);
    cx = 0;
    cy = 0;
}


void fbcon_set_cursor(uint32_t x, uint32_t y)
{
    if (!fb_on)
        return;

    if (x >= cols)
        x = cols - 1;

    if (y >= rows)
        y = rows - 1;

    cx = x;
    cy = y;
	cursor_show();
}


void fbcon_draw_at(char ch, uint32_t col, uint32_t row,
                   uint8_t attr)
{
    if (!fb_on || col >= cols || row >= rows)
        return;
	cursor_hide();

    uint32_t fg = pal[attr & 15];
    uint32_t bg = pal[(attr >> 4) & 15];

    glyph(ch, col, row, fg, bg);
}

