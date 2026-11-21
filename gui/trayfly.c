#include "trayfly.h"
#include "graphics.h"
#include "glass.h"

/* ---- adapt these to your real gfx API ---- */
#define SCREEN_W   ((int)gfx_w())
#define SCREEN_H   ((int)gfx_h())
/* the draw code below passes 0xAARRGGBB; convert to your rgb + alpha API */
#define TF_FILL(x,y,w,h,argb) gfx_blend_rect((x),(y),(w),(h),(argb) & 0xFFFFFF, ((argb) >> 24) & 0xFF)
#define TF_TEXT(x,y,s,col)    gfx_text((x),(y),(s),(col) & 0xFFFFFF)
/* ------------------------------------------ */

static int kind = TF_NONE, fx, fy, fw, fh;
static int volume = 70, muted = 0, dragging = 0;

int tf_is_open(void) { return kind != TF_NONE; }
int tf_volume(void)  { return volume; }
int tf_muted(void)   { return muted; }

void tf_open(int k, int icon_cx) {
    if (kind == k) { kind = TF_NONE; return; }      // click same icon = close
    kind = k;
    switch (k) {
        case TF_NOTIF: fw = 260; fh = 120; break;
        case TF_NET:   fw = 240; fh = 110; break;
        case TF_VOL:   fw = 56;  fh = 170; break;
        case TF_CLOCK: fw = 230; fh = 200; break;
    }
    fx = icon_cx - fw / 2;
    if (fx + fw > SCREEN_W - 4) fx = SCREEN_W - 4 - fw;
    if (fx < 4) fx = 4;
    fy = SCREEN_H - GLASS_TASKBAR_H - fh - 4;
}

static int inside(int x, int y, int w, int h, int mx, int my) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

int tf_pointer(int mx, int my, int btn, int prev) {
    if (kind == TF_NONE) return 0;
    int pressed = btn && !prev;

    if (kind == TF_VOL) {
        int sx = fx + fw / 2 - 3, sy = fy + 14, sh = 110;   // slider track
        if (pressed && inside(sx - 8, sy - 6, 22, sh + 12, mx, my)) dragging = 1;
        if (!btn) dragging = 0;
        if (dragging) {
            int v = 100 - ((my - sy) * 100) / sh;
            volume = v < 0 ? 0 : v > 100 ? 100 : v;
            muted = 0;
            return 1;
        }
        // mute button
        if (pressed && inside(fx + 8, fy + fh - 30, fw - 16, 22, mx, my)) {
            muted = !muted;
            return 1;
        }
    }
    if (pressed && !inside(fx, fy, fw, fh, mx, my)) {
        kind = TF_NONE;          // click outside closes; the click still passes on to the desktop
        return 1;
    }
    return pressed && inside(fx, fy, fw, fh, mx, my);   // swallow clicks inside the flyout
}

void tf_close(void) { kind = TF_NONE; dragging = 0; }
void tf_rect(int *x, int *y, int *w, int *h) { *x = fx; *y = fy; *w = fw; *h = fh; }

void trayfly_draw(void) {
    if (kind == TF_NONE) return;
    TF_FILL(fx, fy, fw, fh, 0xE0203040);                 // glass body
    TF_FILL(fx, fy, fw, 1, 0xA0FFFFFF);                  // top highlight
    TF_FILL(fx, fy, 1, fh, 0x60FFFFFF);
    TF_FILL(fx + fw - 1, fy, 1, fh, 0x60FFFFFF);

    char buf[32];
    switch (kind) {
    case TF_NOTIF:
        TF_TEXT(fx + 10, fy + 10, "Notifications", 0xFFFFFFFF);
        TF_TEXT(fx + 10, fy + 40, "No new notifications", 0xFFCCCCCC);
        break;
    case TF_NET:
        TF_TEXT(fx + 10, fy + 10, "Network", 0xFFFFFFFF);
        TF_TEXT(fx + 10, fy + 40, "e1000: not connected", 0xFFCCCCCC);  // hook to real nic state
        TF_TEXT(fx + 10, fy + 80, "Open network settings", 0xFF99CCFF);
        break;
    case TF_VOL: {
        int sx = fx + fw / 2 - 3, sy = fy + 14, sh = 110;
        TF_FILL(sx, sy, 6, sh, 0xFF101820);                              // track
        int v = muted ? 0 : volume, kh = (sh * v) / 100;
        TF_FILL(sx, sy + sh - kh, 6, kh, 0xFF4FA8FF);                    // fill
        TF_FILL(sx - 5, sy + sh - kh - 3, 16, 6, 0xFFE8E8E8);            // knob
        TF_TEXT(fx + 8, fy + fh - 28, muted ? "Unmute" : "Mute", 0xFFFFFFFF);
        break; }
    case TF_CLOCK:
        TF_TEXT(fx + 10, fy + 10, "Calendar", 0xFFFFFFFF);
        /* feed it your RTC-derived y/m/d, then draw a 7x6 grid of day numbers */
        (void)buf;
        break;
    }
}