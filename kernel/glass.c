#include "glass.h"

static int cl(int v) { return v > 255 ? 255 : v; }

/* Sky-blue to deep-navy gradient with a soft glow around the horizon. */
static uint32_t wall_at(int y, int h)
{
    int n = h > 1 ? h - 1 : 1;
    int r = 0x2E + (0x0B - 0x2E) * y / n;
    int g = 0x7B + (0x1E - 0x7B) * y / n;
    int b = 0xC0 + (0x3C - 0xC0) * y / n;
    int hz = h * 45 / 100;
    int band = h / 5;
    int d = y > hz ? y - hz : hz - y;

    if (band > 0 && d < band) {
        int add = 30 * (band - d) / band;
        r += add; g += add; b += add;
    }
    return ((uint32_t)cl(r) << 16) | ((uint32_t)cl(g) << 8) | (uint32_t)cl(b);
}

/* from graphics.c (safe to repeat if graphics.h already declares them) */
extern void gfx_cache_save(void);
extern int  gfx_cache_restore(void);

void glass_wallpaper(void)
{
    int w = (int)gfx_w(), h = (int)gfx_h();

    if (gfx_cache_restore())                     /* one fast copy after the first draw */
        return;

    for (int y = 0; y < h; y++)
        gfx_rect(0, y, w, 1, wall_at(y, h));

    gfx_cache_save();
}

void glass_panel(int x, int y, int w, int h)
{
    gfx_blend_rect(x + 5, y + 5, w, h, 0x000000, 70);      /* shadow */
    gfx_blend_rect(x, y, w, h, 0xBFD8F0, 120);             /* glass body */
    gfx_frame(x, y, w, h, 0x2A4A70);                       /* dark outer edge */
    gfx_frame(x + 1, y + 1, w - 2, h - 2, 0xDCEBFA);       /* light inner edge */
}

/* caption button under the pointer for the window being drawn: 0 none, 1 min, 2 max, 3 close */
int glass_hover_btn;

/* 1 = draw the next glass_window() as the focused window, 0 = inactive look */
int glass_focused = 1;

static uint32_t lerp(uint32_t a, uint32_t b, int t, int n)
{
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;

    if (n < 1) n = 1;
    return ((uint32_t)(ar + (br - ar) * t / n) << 16) |
           ((uint32_t)(ag + (bg - ag) * t / n) << 8)  |
            (uint32_t)(ab + (bb - ab) * t / n);
}

/* glossy split body; round: 1 = bottom-left corner, 2 = bottom-right */
static void cap_body(int x, int y, int w, int h, uint32_t t0, uint32_t t1, int at,
                     uint32_t b0, uint32_t b1, int ab, int round)
{
    int half = h / 2;

    for (int i = 0; i < h; i++) {
        int top = i < half, fb = h - 1 - i, l = 0, r = 0;
        uint32_t c = top ? lerp(t0, t1, i, half - 1) : lerp(b0, b1, i - half, h - half - 1);
        int a = top ? at : ab;

        if (fb == 0) l = r = 2; else if (fb == 1) l = r = 1;
        if (!(round & 1)) l = 0;
        if (!(round & 2)) r = 0;
        if (a >= 255) gfx_rect(x + l, y + i, w - l - r, 1, c);
        else          gfx_blend_rect(x + l, y + i, w - l - r, 1, c, a);
    }
}

static void glyph(int kind, int cx, int cy)       /* 0 min, 1 max, 2 close */
{
    for (int pass = 0; pass < 2; pass++) {
        int o = (pass == 0);
        uint32_t c = o ? 0x10243A : 0xFFFFFF;
        int px = cx + o, py = cy + o;

        if (kind == 0) {
            gfx_rect(px - 5, py + 3, 10, 3, c);
        } else if (kind == 1) {
            gfx_frame(px - 5, py - 4, 10, 9, c);
            gfx_rect(px - 5, py - 4, 10, 3, c);
        } else {
            for (int i = 0; i < 9; i++) {
                gfx_rect(px - 5 + i, py - 4 + i, 2, 2, c);
                gfx_rect(px + 4 - i, py - 4 + i, 2, 2, c);
            }
        }
    }
}

void glass_caption(int x, int y, int w)
{
    int cw = GLASS_CLOSE_W, ch = GLASS_CLOSE_H;
    int bx = GLASS_CLOSE_X(x, w), by = GLASS_CLOSE_Y(y);
    int mx = bx - GLASS_CAP_W, nx = mx - GLASS_CAP_W;
    int hv = glass_hover_btn, cy = by + ch / 2;

    gfx_blend_rect(nx - 1, by - 1, 2 * GLASS_CAP_W + cw + 2, ch + 2, 0x000000, 120);

    for (int b = 0; b < 2; b++) {
        int hot = (hv == 1 + b);
        cap_body(b ? mx : nx, by, GLASS_CAP_W, ch,
                 hot ? 0xE8F6FF : 0xFFFFFF, hot ? 0xB8DCFA : 0xE0EEFA, hot ? 220 : 110,
                 hot ? 0x6CB0F0 : 0x9CC0E8, hot ? 0xA8D8FF : 0xC8E0F8, hot ? 190 : 70,
                 b ? 0 : 1);
    }

    cap_body(bx, by, cw, ch,
             hv == 3 ? 0xF4B8A0 : 0xE8A090, hv == 3 ? 0xE87860 : 0xD06050, 255,
             hv == 3 ? 0xD03010 : 0xA82010, hv == 3 ? 0xF06030 : 0xD04830, 255, 2);

    gfx_blend_rect(mx, by, 1, ch, 0x000000, 90);
    gfx_blend_rect(bx, by, 1, ch, 0x000000, 90);

    glyph(0, nx + GLASS_CAP_W / 2, cy);
    glyph(1, mx + GLASS_CAP_W / 2, cy);
    glyph(2, bx + cw / 2, cy);
}

void glass_window(int x, int y, int w, int h, const char *title)
{
    int foc = glass_focused;
    int cx = x + 8, cy = y + GLASS_TITLE_H + 4;
    int cw = w - 16, ch = h - GLASS_TITLE_H - 12;

    glass_panel(x, y, w, h);

    /* title bar gloss: brighter top half; inactive windows are flatter and darker */
    if (!foc)
        gfx_blend_rect(x + 2, y + 2, w - 4, GLASS_TITLE_H - 2, 0x203A5C, 70);
    gfx_blend_rect(x + 2, y + 2, w - 4, 14, 0xFFFFFF, foc ? 80 : 35);
    gfx_blend_rect(x + 2, y + 16, w - 4, GLASS_TITLE_H - 16, 0xFFFFFF, foc ? 35 : 12);
    gfx_text(x + 11, y + 8, title, 0x10243C);
    gfx_text(x + 10, y + 7, title, foc ? 0xFFFFFF : 0xC4D2E2);

    /* Windows 7-style minimize / maximize / close */
    glass_caption(x, y, w);

    /* readable client area */
    gfx_blend_rect(cx, cy, cw, ch, 0xF2F7FC, 215);
    gfx_frame(cx, cy, cw, ch, 0x7FA3C8);
}

void glass_taskbar(void)
{
    int w = (int)gfx_w();
    int y = (int)gfx_h() - GLASS_TASKBAR_H;

    gfx_blend_rect(0, y, w, GLASS_TASKBAR_H, 0x0A1830, 150);
    gfx_blend_rect(0, y, w, GLASS_TASKBAR_H / 2, 0xFFFFFF, 30);
    gfx_rect(0, y, w, 1, 0xB8D8F0);
}

void glass_highlight(int x, int y, int w, int h)
{
    gfx_blend_rect(x, y, w, h, 0xFFFFFF, 40);
    gfx_frame(x, y, w, h, 0x8FC0F0);
}