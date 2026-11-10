#include "deskt.h"
#include "glass.h"
#include "wiman.h"
#include "mouse.h"
#include "klog.h"
#include "ftfs.h"
#include "ui.h"
#include "keys.h"
#include "calc.h"
#include "assets/icons/cursors/cursor.xpm"
#include "assets/icons/fishlogo.xpm"
#include "assets/icons/fishlogoslctd.xpm"

#define ICON_SZ 48
#define ORB_X   6
#define ORB_SZ  44

#define MENU_X    4
#define MENU_W    200
#define ITEM_H    32
#define MENU_PAD  4
#define MENU_N    5

enum { WIN_NONE, WIN_ABOUT, WIN_FTFS, WIN_BIN, WIN_CALC };

struct dicon { int x, y; const char *label; int win; };

#define NICONS 2
static const struct dicon icons[NICONS] = {
    { 48,  40, "About Yellowfin", WIN_ABOUT },
    { 48, 140, "Recycle Bin",     WIN_BIN   },
};

static const char *menu_items[MENU_N] = {
    "Install Yellowfin",
    "FTFS Files",
    "About Yellowfin",
    "Exit to shell",
    "Calculator"
};
static uint32_t cur_px[12 * 19], logo_px[64 * 64], logo_sel_px[64 * 64];
static gfx_image img_cur, img_logo, img_logo_sel;

static int menu_open, hover_icon = -1, hover_orb, hover_item = -1;
static int (*pk)(void);
static int prev, want_exit;
static int hover_cap, hover_cap_slot = -1;      /* caption button under the pointer */

extern int glass_hover_btn, glass_focused;      /* from glass.c (safe to repeat if glass.h has them) */
extern void gfx_flip_rect(int x, int y, int w, int h);   /* from graphics.c */

/* ---- small helpers ---- */

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static char *append(char *d, const char *s) { while (*s) *d++ = *s++; return d; }

static char *append_u(char *d, u32 n)
{
    char t[11];
    int i = 0;

    if (!n) *d++ = '0';
    while (n) { t[i++] = '0' + (n % 10); n /= 10; }
    while (i) *d++ = t[--i];
    return d;
}

static int orb_y(void) { return (int)gfx_h() - GLASS_TASKBAR_H / 2 - ORB_SZ / 2; }

static void shadow_text(int x, int y, const char *s, uint32_t fg)
{
    gfx_text(x + 1, y + 1, s, 0x0A1424);
    gfx_text(x, y, s, fg);
}

/* ---- window opening (all windows go through the window manager) ---- */

static void open_win(int kind)
{
    const char *t = "Window";

    if (kind == WIN_ABOUT)     t = "About Yellowfin OS";
    else if (kind == WIN_FTFS) t = "FTFS Files";
    else if (kind == WIN_BIN)  t = "Recycle Bin";
    else if (kind == WIN_CALC) t = "Calculator";

    if (kind == WIN_CALC)
        wm_open(kind, t, CALC_W, CALC_H);
    else
        wm_open(kind, t, 420, 240);
}

/* ---- start menu geometry ---- */

static int menu_h(void) { return MENU_N * ITEM_H + 2 * MENU_PAD; }
static int menu_y(void) { return (int)gfx_h() - GLASS_TASKBAR_H - menu_h() - 2; }

/* -1 outside the panel, -2 inside but not on an item, else item index */
static int menu_hit(int mx, int my)
{
    int y = menu_y(), i;

    if (!inside(mx, my, MENU_X, y, MENU_W, menu_h()))
        return -1;
    if (my - y < MENU_PAD)
        return -2;
    i = (my - y - MENU_PAD) / ITEM_H;
    return i < MENU_N ? i : -2;
}

/* ---- drawing ---- */

static void draw_bin_icon(int x, int y)       /* placeholder, swap for an XPM later */
{
    gfx_rect(x + 16, y + 2, 16, 5, 0xD8E8F8);
    gfx_frame(x + 16, y + 2, 16, 5, 0xFFFFFF);
    gfx_rect(x + 6, y + 8, 36, 7, 0xE8F0FA);
    gfx_frame(x + 6, y + 8, 36, 7, 0xFFFFFF);
    gfx_blend_rect(x + 10, y + 16, 28, 28, 0xA0C8F0, 170);
    gfx_frame(x + 10, y + 16, 28, 28, 0xFFFFFF);
    gfx_rect(x + 17, y + 20, 2, 20, 0xFFFFFF);
    gfx_rect(x + 23, y + 20, 2, 20, 0xFFFFFF);
    gfx_rect(x + 29, y + 20, 2, 20, 0xFFFFFF);
}

static void draw_about(int x, int y)
{
    static const char *lines[] = {
        "Yellowfin OS 0.0.1", "Codename: Fish", "Architecture: i686",
        "Graphics: VBE framebuffer", "Shell: fsh", "Kernel: Corn"
    };

    for (int i = 0; i < 6; i++)
        gfx_text(x + 24, y + GLASS_TITLE_H + 16 + i * 22, lines[i], 0x14283E);

    gfx_text(x + 24, y + GLASS_TITLE_H + 16 + 6 * 22 + 8,
             "Press D to close the top window", 0x2A5A8A);
}

static void draw_ftfs(int x, int y, int h)
{
    char name[20], line[24], *e;
    u32 size;
    int i, ty = y + GLASS_TITLE_H + 12;

    gfx_text(x + 24, ty, "Name", 0x2A5A8A);
    gfx_text(x + 24 + 168, ty, "Size", 0x2A5A8A);
    ty += 22;

    for (i = 0; i < 6 && ftfs_entry(i, name, &size) == 0; i++) {
        gfx_text(x + 24, ty, name, 0x14283E);
        e = append_u(line, size);
        e = append(e, " bytes");
        *e = 0;
        gfx_text(x + 24 + 168, ty, line, 0x14283E);
        ty += 20;
    }

    if (i == 0)
        gfx_text(x + 24, ty, "(no files)", 0x14283E);

    e = append_u(line, ftfs_free_blocks());
    e = append(e, " of 65403 blocks free");
    *e = 0;
    gfx_text(x + 24, y + h - 28, line, 0x2A5A8A);
}

static void draw_bin(int x, int y)
{
    gfx_text(x + 24, y + GLASS_TITLE_H + 20, "The Recycle Bin is empty.", 0x14283E);
    gfx_text(x + 24, y + GLASS_TITLE_H + 48,
             "(rm deletes files directly for now)", 0x2A5A8A);
}

/* draw every open window, back to front */
static void draw_windows(void)
{
    int top = wm_top();

    for (int n = 0; n < wm_nz(); n++) {
        int s = wm_z(n);
        struct wm_win *w = wm_get(s);

        if (w->minimized)
            continue;

        glass_focused = (s == top);
        glass_hover_btn = (s == hover_cap_slot) ? hover_cap : 0;
        glass_window(w->x, w->y, w->w, w->h, w->title);

        if (w->kind == WIN_ABOUT)     draw_about(w->x, w->y);
        else if (w->kind == WIN_FTFS) draw_ftfs(w->x, w->y, w->h);
        else if (w->kind == WIN_BIN)  draw_bin(w->x, w->y);
        else if (w->kind == WIN_CALC) calc_draw(w->x, w->y);
    }

    glass_focused = 1;                           /* leave defaults for other glass_window users */
    glass_hover_btn = 0;
}

/* one taskbar button per open window */
static void draw_tb_buttons(void)
{
    int top = wm_top();

    for (int n = 0; n < wm_ntb(); n++) {
        int s = wm_tb_slot(n), x, y, w, h, i;
        char label[18];
        struct wm_win *ww = wm_get(s);

        wm_tb_rect(n, &x, &y, &w, &h);
        gfx_blend_rect(x, y, w, h, 0xA0C8F0, s == top ? 170 : 80);
        gfx_frame(x, y, w, h, 0xFFFFFF);

        for (i = 0; i < 16 && ww->title[i]; i++)
            label[i] = ww->title[i];
        label[i] = 0;
        gfx_text(x + 10, y + (h - 16) / 2, label, 0x10243A);
    }
}

static void draw_menu(void)
{
    int y = menu_y(), h = menu_h();

    gfx_blend_rect(MENU_X + 4, y + 4, MENU_W, h, 0x000000, 90);
    gfx_blend_rect(MENU_X, y, MENU_W, h, 0xA0C8F0, 170);
    gfx_frame(MENU_X, y, MENU_W, h, 0xFFFFFF);

    for (int i = 0; i < MENU_N; i++) {
        int iy = y + MENU_PAD + i * ITEM_H;

        if (i == hover_item)
            glass_highlight(MENU_X + 4, iy, MENU_W - 8, ITEM_H);
        gfx_text(MENU_X + 16, iy + 8, menu_items[i], 0x10243A);
    }
}

/* compose the whole desktop into the back buffer, then flip only (fx,fy,fw,fh) */
static void draw_desktop_rect(int fx, int fy, int fw, int fh)
{
    int w = (int)gfx_w(), h = (int)gfx_h();

    glass_wallpaper();

    for (int i = 0; i < NICONS; i++) {
        const struct dicon *ic = &icons[i];

        if (i == hover_icon)
            glass_highlight(ic->x - 40, ic->y - 8, 128, 86);

        if (ic->win == WIN_BIN)
            draw_bin_icon(ic->x, ic->y);
        else
            gfx_blit_scaled(&img_logo, ic->x, ic->y, ICON_SZ, ICON_SZ);

        shadow_text(ic->x + ICON_SZ / 2 - slen(ic->label) * 4, ic->y + 54,
                    ic->label, 0xFFFFFF);
    }

    shadow_text(w - 8 * 18 - 16, h - GLASS_TASKBAR_H - 24,
                "Yellowfin OS 0.0.1", 0xD8E8F8);

    draw_windows();

    glass_taskbar();
    draw_tb_buttons();
    shadow_text(w - 8 * 18 - 16, h - GLASS_TASKBAR_H + 12,
                "M: Menu   Q: Shell", 0xD8E8F8);

    gfx_blit_scaled(hover_orb ? &img_logo_sel : &img_logo, ORB_X, orb_y(), ORB_SZ, ORB_SZ);

    if (menu_open)
        draw_menu();

    gfx_flip_rect(fx, fy, fw, fh);
}

static void draw_desktop(void)
{
    draw_desktop_rect(0, 0, (int)gfx_w(), (int)gfx_h());
}

static void redraw(void)
{
    mouse_cursor_hide();
    draw_desktop();
    mouse_cursor_show();
}

/* used while dragging a window: the cursor sits inside the dirty area, so only that area is flipped */
static void redraw_rect(int x, int y, int w, int h)
{
    mouse_cursor_hide();
    draw_desktop_rect(x, y, w, h);
    mouse_cursor_show();
}

/* ---- installer launch ---- */

static u8 wait_key(void)
{
    for (;;) {
        int k;

        mouse_poll();
        k = pk();
        if (k > 0 && k < 256)
            return (u8)k;
    }
}

static void run_installer(void)
{
    mouse_cursor_hide();
    installer_run(wait_key);
    redraw();
}

static void menu_action(int i)
{
    menu_open = 0;

    if (i == 0)      run_installer();
    else if (i == 1) open_win(WIN_FTFS);
    else if (i == 2) open_win(WIN_ABOUT);
    else if (i == 3) want_exit = 1;
    else if (i == 4) open_win(WIN_CALC);
}

/* ---- pointer handling ---- */

static void pointer_update(int btn)
{
    int mx = mouse_x(), my = mouse_y();
    int hi = -1, ho, hm = -1, inmenu = -1;
    int oprev = prev;
    int pressed = (btn & 1) && !(oprev & 1);
    int dirty, wf;

    /* a window drag owns the pointer until the button is released */
    if (wm_dragging()) {
        wf = wm_pointer(mx, my, btn, oprev);
        prev = btn;
        if (wf & WM_REDRAW) {
            int rx, ry, rw, rh;

            wm_dirty_rect(&rx, &ry, &rw, &rh);
            redraw_rect(rx, ry, rw, rh);
        } else {
            mouse_cursor_update();
        }
        return;
    }

    for (int i = 0; i < NICONS; i++)
        if (inside(mx, my, icons[i].x - 40, icons[i].y - 8, 128, 86))
            hi = i;

    ho = inside(mx, my, ORB_X, orb_y(), ORB_SZ, ORB_SZ);

    if (menu_open) {
        inmenu = menu_hit(mx, my);
        if (inmenu >= 0)
            hm = inmenu;
    }

    {
        int hc = 0, hs = -1;

        if (!(menu_open && inmenu != -1))
            hc = wm_cap_at(mx, my, &hs);
        if (!hc)
            hs = -1;

        dirty = (hi != hover_icon) || (ho != hover_orb) || (hm != hover_item) ||
                (hc != hover_cap) || (hs != hover_cap_slot);
        hover_cap = hc;
        hover_cap_slot = hs;
    }
    prev = btn;
    hover_icon = hi;
    hover_orb = ho;
    hover_item = hm;

    if (pressed) {
        int tb;

        dirty = 1;

        if (menu_open && inmenu != -1) {              /* click inside the panel */
            if (hm >= 0)
                menu_action(hm);
        } else if (ho) {
            menu_open = !menu_open;
        } else if (menu_open) {
            menu_open = 0;
        } else if ((tb = wm_tb_hit(mx, my)) >= 0) {   /* taskbar window button */
            wm_tb_click(tb);
        } else {
            wf = wm_pointer(mx, my, btn, oprev);

            if (wf & WM_USED) {
                int s = wm_top();
                struct wm_win *w = (s >= 0) ? wm_get(s) : 0;

                /* The window manager handles focus and caption controls.
                   Calculator clicks are then sent to the focused client. */
                if (w && !w->minimized && w->kind == WIN_CALC &&
                    inside(mx, my, w->x, w->y + GLASS_TITLE_H,
                           w->w, w->h - GLASS_TITLE_H)) {
                    if (calc_click(mx, my, w->x, w->y))
                        dirty = 1;
                }
            } else if (hi >= 0) {
                open_win(icons[hi].win);
            }
        }
    }

    if (dirty)
        redraw();
    else
        mouse_cursor_update();
}

void desktop_run(int (*poll_key)(void))
{
    if (!gfx_init())
        return;

    xpm_load(&img_cur, (const char *const *)cursor_xpm, cur_px, 12 * 19);
    xpm_load(&img_logo, (const char *const *)fishlogo_xpm, logo_px, 64 * 64);
    xpm_load(&img_logo_sel, (const char *const *)fishlogoslctd_xpm, logo_sel_px, 64 * 64);

    pk = poll_key;
    wm_init();
    calc_reset();
    menu_open = 0;
    hover_icon = hover_item = -1;
    hover_orb = 0;
    hover_cap = 0;
    hover_cap_slot = -1;
    prev = 0;
    want_exit = 0;

    mouse_init();
    mouse_set_cursor(&img_cur, 0, 0);
    draw_desktop();
    mouse_cursor_show();

    for (;;) {
        int k;

        if (want_exit)
            break;

        if (mouse_poll())
            pointer_update(mouse_buttons());

        k = poll_key();
        if (k <= 0)
            continue;

        /* Give the focused calculator first chance to consume keyboard input. */
        {
            int s = wm_top();
            struct wm_win *w = (s >= 0) ? wm_get(s) : 0;

            if (w && !w->minimized && w->kind == WIN_CALC && calc_key(k)) {
                redraw();
                continue;
            }
        }

        if (k == 'q' || k == 'Q')
            break;

        if (k == 27) {
            if (menu_open) { menu_open = 0; redraw(); continue; }
            break;
        }

        if (k == 0x80)      { mouse_move_by(0, -12);  pointer_update(0); }
        else if (k == 0x81) { mouse_move_by(0,  12);  pointer_update(0); }
        else if (k == 0x82) { mouse_move_by(-12, 0);  pointer_update(0); }
        else if (k == 0x83) { mouse_move_by( 12, 0);  pointer_update(0); }
        else if (k == '\n' || k == ' ') { pointer_update(1); pointer_update(0); }

        if (k == KEY_WIN || k == 'm' || k == 'M') { menu_open = !menu_open; hover_item = -1; redraw(); }
        else if (k == 'a' || k == 'A') { open_win(WIN_ABOUT); redraw(); }
        else if (k == 'd' || k == 'D') { wm_close(wm_top()); menu_open = 0; redraw(); }
    }

    mouse_cursor_hide();
}