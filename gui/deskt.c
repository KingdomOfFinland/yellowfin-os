#include "deskt.h"
#include "glass.h"
#include "wiman.h"
#include "mouse.h"
#include "klog.h"
#include "ftfs.h"
#include "ui.h"
#include "keys.h"
#include "calc.h"
#include "cmd.h"
#include "timer.h"
#include "rtc.h"
#include "explorer.h"
#include "startmenu.h"
#include "trayfly.h"
#include "assets/icons/cursors/cursor.xpm"
#include "assets/icons/cursors/hand.xpm"
#include "assets/icons/cursors/busy_frames/busyf1.xpm"
#include "assets/icons/cursors/busy_frames/busyf2.xpm"
#include "assets/icons/cursors/busy_frames/busyf3.xpm"
#include "assets/icons/cursors/busy_frames/busyf4.xpm"
#include "assets/icons/fishlogo.xpm"
#include "assets/icons/fishlogoslctd.xpm"
#include "assets/icons/wifi/1bar.xpm"
#include "assets/icons/wifi/2bars.xpm"
#include "assets/icons/wifi/3bars.xpm"
#include "assets/icons/wifi/4bars.xpm"
#include "assets/icons/wifi/disconnected.xpm"
#include "assets/icons/wifi/err.xpm"
#include "assets/icons/wifi/warn.xpm"
#include "assets/icons/desktopicons/notif.xpm"
#include "assets/icons/desktopicons/notif1.xpm"

#define ICON_SZ 48
#define ORB_X   6
#define ORB_SZ  44
#define TRAY_W     174
#define CLOCK_POLL 100     /* timer ticks between RTC checks, about 1 s: set to your PIT rate */

#define BUSY_STEP  15      /* timer ticks per hourglass frame (about 150 ms at 100 Hz) */
#define BUSY_HX    16      /* hourglass hotspot: middle of the 32x32 frame */
#define BUSY_HY    16
#define HAND_HX    5       /* hand hotspot: fingertip, adjust to match your hand.xpm */
#define HAND_HY    0

#define DESK_DEBUG_KEYS 1 /* B = toggle hourglass, N = fake notification, W = cycle wifi icon */

enum { WIN_NONE, WIN_ABOUT, WIN_FTFS, WIN_BIN, WIN_CALC, WIN_CMD, WIN_EXPL };
enum { CUR_ARROW, CUR_HAND, CUR_BUSY };
enum { NET_DOWN, NET_1, NET_2, NET_3, NET_4, NET_ERR, NET_WARN, NET_N };

struct dicon { int x, y; const char *label; int win; };

#define NICONS 2
static const struct dicon icons[NICONS] = {
    { 48,  40, "About Yellowfin", WIN_ABOUT },
    { 48, 140, "Recycle Bin",     WIN_BIN   },
};

static uint32_t cur_px[12 * 19], logo_px[64 * 64], logo_sel_px[64 * 64];
static gfx_image img_cur, img_logo, img_logo_sel;

/* cursors */
static uint32_t hand_px[32 * 32], busy_px[4][32 * 32];
static gfx_image img_hand, img_busy[4];
static int cur_kind = CUR_ARROW;
static int busy_on, busy_frame;
static unsigned busy_last;

/* tray icons */
static uint32_t net_px[NET_N][16 * 16], notif_px[2][16 * 16];
static gfx_image img_net[NET_N], img_notif[2];
static int net_state = NET_DOWN;       /* a network driver sets this */
static int notif_unread;               /* > 0 shows the red-dot icon */
static int desk_active;

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

/* ---- cursors: arrow, hand, hourglass ---- */

static void cursor_apply(void)
{
    mouse_cursor_hide();
    if (cur_kind == CUR_HAND)
        mouse_set_cursor(&img_hand, HAND_HX, HAND_HY);
    else if (cur_kind == CUR_BUSY)
        mouse_set_cursor(&img_busy[busy_frame], BUSY_HX, BUSY_HY);
    else
        mouse_set_cursor(&img_cur, 0, 0);
    mouse_cursor_show();
}

static void cursor_set(int kind)
{
    if (kind == cur_kind)
        return;
    cur_kind = kind;
    cursor_apply();
}

void busy_begin(void)
{
    busy_on = 1;
    busy_frame = 0;
    busy_last = timer_ticks();
    cursor_set(CUR_BUSY);
}

void busy_tick(void)                    /* call from any slow loop, and from the main loop */
{
    if (!busy_on || timer_ticks() - busy_last < BUSY_STEP)
        return;
    busy_last = timer_ticks();
    busy_frame = (busy_frame + 1) & 3;
    cursor_apply();
}

void busy_end(void)
{
    busy_on = 0;
    cursor_set(CUR_ARROW);
}

/* ---- clock (CMOS RTC is UTC, converted to local time) ---- */

static int clk_h, clk_m, clk_d, clk_mo, clk_y;
static unsigned clk_last;

static inline unsigned char cmos_in(unsigned char reg)
{
    unsigned char v;
    __asm__ volatile ("outb %0, $0x70" : : "a"(reg));
    __asm__ volatile ("inb $0x71, %0" : "=a"(v));
    return v;
}

static int cmos_updating(void) { return cmos_in(0x0A) & 0x80; }
static int bcd(int v)          { return (v & 0x0F) + ((v >> 4) * 10); }

static int tz_base_min = 120;      /* Finland: UTC+2 (EET) */
static int tz_auto_dst = 1;        /* EU summer time: +1 h between the last Sundays of March and October */

static int days_from_civil(int y, int m, int d)     /* days since 1970-01-01 */
{
    int era, yoe, doy, doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int z, int *y, int *m, int *d)
{
    int era, doe, yoe, doy, mp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yoe + era * 400 + (*m <= 2);
}

static int last_sunday(int y, int m)                /* as days since 1970 */
{
    int last = days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - 1;

    return last - (last + 4) % 7;                   /* 1970-01-01 was a Thursday */
}

static void apply_tz(int *h, int *m, int *d, int *mo, int *y)
{
    int days = days_from_civil(*y, *mo, *d);
    int t = days * 1440 + *h * 60 + *m;             /* UTC minutes since 1970 */
    int off = tz_base_min, rem;

    if (tz_auto_dst) {
        int s = last_sunday(*y, 3) * 1440 + 60;     /* both switch at 01:00 UTC */
        int e = last_sunday(*y, 10) * 1440 + 60;

        if (t >= s && t < e)
            off += 60;
    }
    t += off;
    days = t / 1440;
    rem = t % 1440;
    if (rem < 0) { rem += 1440; days--; }
    civil_from_days(days, y, mo, d);
    *h = rem / 60;
    *m = rem % 60;
}

static void clock_read(int *h, int *m, int *d, int *mo, int *y)
{
    int mi, hr, da, mn, yr, regb;

    while (cmos_updating())                 /* avoid reading mid-update */
        ;
    mi = cmos_in(0x02); hr = cmos_in(0x04);
    da = cmos_in(0x07); mn = cmos_in(0x08); yr = cmos_in(0x09);
    regb = cmos_in(0x0B);

    if (!(regb & 0x04)) {                   /* values are BCD */
        mi = bcd(mi);
        hr = bcd(hr & 0x7F) | (hr & 0x80);
        da = bcd(da); mn = bcd(mn); yr = bcd(yr);
    }
    if (!(regb & 0x02) && (hr & 0x80))      /* 12-hour mode, PM bit */
        hr = ((hr & 0x7F) + 12) % 24;

    *h = hr; *m = mi; *d = da; *mo = mn; *y = 2000 + yr;
    apply_tz(h, m, d, mo, y);
}

static int clock_tick(void)               /* returns 1 when the shown time changed */
{
    unsigned now = timer_ticks();
    int h, m, d, mo, y, changed;

    if (now - clk_last < CLOCK_POLL)
        return 0;
    clk_last = now;
    clock_read(&h, &m, &d, &mo, &y);
    changed = (m != clk_m) || (h != clk_h) || (d != clk_d);
    clk_h = h; clk_m = m; clk_d = d; clk_mo = mo; clk_y = y;
    return changed;
}

static void two(char *d, int v) { d[0] = '0' + (v / 10) % 10; d[1] = '0' + v % 10; }

/* ---- tray: notifications, network, volume, clock ---- */

static void net_load(void)
{
    const char *const *src[NET_N] = {
        (const char *const *)disconnected_xpm,
        (const char *const *)wifi1_xpm,
        (const char *const *)wifi2_xpm,
        (const char *const *)wifi3_xpm,
        (const char *const *)wifi4_xpm,
        (const char *const *)err_xpm,
        (const char *const *)warn_xpm,
    };

    for (int i = 0; i < NET_N; i++)
        xpm_load(&img_net[i], src[i], net_px[i], 16 * 16);
}

static void notif_rect(int *x, int *y, int *w, int *h)    /* clickable area of the notification icon */
{
    *x = (int)gfx_w() - TRAY_W + 6;
    *y = (int)gfx_h() - GLASS_TASKBAR_H + 4;
    *w = 24;
    *h = GLASS_TASKBAR_H - 8;
}

static void draw_tray(void)
{
    int w = (int)gfx_w(), ty = (int)gfx_h() - GLASS_TASKBAR_H;
    int x0 = w - TRAY_W, cy = ty + GLASS_TASKBAR_H / 2, cx = w - 44;
    char t[6], dt[11];

    gfx_blend_rect(x0, ty + 6, 1, GLASS_TASKBAR_H - 12, 0xFFFFFF, 90);   /* separator */

    gfx_blit_scaled(&img_notif[notif_unread > 0], x0 + 10, cy - 8, 16, 16);
    gfx_blit_scaled(&img_net[net_state], x0 + 34, cy - 8, 16, 16);

    /* speaker icon: body, cone, one sound wave */
    gfx_rect(x0 + 66, cy - 3, 4, 7, 0xFFFFFF);
    gfx_rect(x0 + 70, cy - 6, 3, 13, 0xFFFFFF);
    gfx_rect(x0 + 76, cy - 4, 2, 9, 0xD8E8F8);

    two(t, clk_h); t[2] = ':'; two(t + 3, clk_m); t[5] = 0;
    two(dt, clk_d); dt[2] = '.'; two(dt + 3, clk_mo); dt[5] = '.';
    two(dt + 6, clk_y / 100); two(dt + 8, clk_y % 100); dt[10] = 0;

    shadow_text(cx - gfx_text_width(t) / 2,  ty + 6,                      t,  0xFFFFFF);
    shadow_text(cx - gfx_text_width(dt) / 2, ty + GLASS_TASKBAR_H / 2 + 1, dt, 0xD8E8F8);
}

/* ---- window opening (all windows go through the window manager) ---- */

static void open_win(int kind)
{
    const char *t = "Window";

    if (kind == WIN_ABOUT)     t = "About Yellowfin OS";
    else if (kind == WIN_FTFS) t = "FTFS Files";
    else if (kind == WIN_BIN)  t = "Recycle Bin";
    else if (kind == WIN_CALC) t = "Calculator";
    else if (kind == WIN_CMD)  t = "Command Prompt";
    else if (kind == WIN_EXPL) t = "Fish Explorer";

    if (kind == WIN_CALC)
        wm_open(kind, t, CALC_W, CALC_H);
    else if (kind == WIN_CMD)
        wm_open(kind, t, CMD_W, CMD_H);
    else if (kind == WIN_EXPL)
        wm_open(kind, t, EX_W, EX_H);
    else
        wm_open(kind, t, 420, 240);
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
        "Yellowfin OS 0.3", "Codename: Fish", "Architecture: i686",
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
        else if (w->kind == WIN_CMD)  cmd_draw(w->x, w->y);
        else if (w->kind == WIN_EXPL) explorer_draw(w->x, w->y, w->w, w->h);
    }

    glass_focused = 1;                           /* leave defaults for other glass_window users */
    glass_hover_btn = 0;
}

/* shorten a title with "..." until it fits maxw pixels */
static void fit_label(char *out, const char *s, int maxw)
{
    int n = 0;

    while (s[n] && n < 36) { out[n] = s[n]; n++; }
    out[n] = 0;
    if (gfx_text_width(out) <= maxw)
        return;
    while (n > 0) {
        n--;
        out[n] = '.'; out[n + 1] = '.'; out[n + 2] = '.'; out[n + 3] = 0;
        if (gfx_text_width(out) <= maxw)
            return;
    }
}

/* one taskbar button per open window */
static void draw_tb_buttons(void)
{
    int top = wm_top();

    for (int n = 0; n < wm_ntb(); n++) {
        int s = wm_tb_slot(n), x, y, w, h;
        char label[40];
        struct wm_win *ww = wm_get(s);

        wm_tb_rect(n, &x, &y, &w, &h);
        gfx_blend_rect(x, y, w, h, 0xA0C8F0, s == top ? 170 : 80);
        gfx_frame(x, y, w, h, 0xFFFFFF);

        fit_label(label, ww->title, w - 20);
        gfx_text(x + 10, y + (h - 16) / 2, label, 0x10243A);
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

        shadow_text(ic->x + ICON_SZ / 2 - gfx_text_width(ic->label) / 2, ic->y + 54,
                    ic->label, 0xFFFFFF);
    }

    shadow_text(w - gfx_text_width("YellowFinOS 0.3") - 16, h - GLASS_TASKBAR_H - 24,
                "YellowFinOS 0.3", 0xD8E8F8);

    draw_windows();
    glass_taskbar();
    draw_tb_buttons();
    draw_tray();

    gfx_blit_scaled(hover_orb ? &img_logo_sel : &img_logo, ORB_X, orb_y(), ORB_SZ, ORB_SZ);

    if (menu_open)
        startmenu_draw(hover_item, &img_logo);

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

static void redraw_tray(void)
{
    redraw_rect((int)gfx_w() - TRAY_W, (int)gfx_h() - GLASS_TASKBAR_H,
                TRAY_W, GLASS_TASKBAR_H);
}

/* anything in the kernel can call this (declare it in deskt.h) */
void desktop_notify(void)
{
    notif_unread++;
    if (desk_active)
        redraw_tray();
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

/* ---- start menu search: FTFS source, key mapping, launch ---- */

#define EXPLORER_OPEN_FILE 0   /* set to 1 once explorer.c has explorer_open_file(name) */

/* fill from keys.h; 0 = that key is ignored for now */
#define SM_K_DEL   0
#define SM_K_HOME  0
#define SM_K_END   0

static char sm_name[32];

static int sm_file_count(void)
{
    u32 size;
    int n = 0;

    while (n < 64 && ftfs_entry(n, sm_name, &size) == 0)
        n++;
    return n;
}

static const char *sm_file_name(int i)
{
    u32 size;

    return ftfs_entry(i, sm_name, &size) == 0 ? sm_name : 0;
}

static int sm_translate(int k)                 /* raw key_poll code -> startmenu_key code */
{
    if (k == 8 || k == 127) return TF_BACKSPACE;
    if (k == 0x80) return SM_KEY_UP;
    if (k == 0x81) return SM_KEY_DOWN;
    if (k == 0x82) return TF_LEFT;
    if (k == 0x83) return TF_RIGHT;
    if (SM_K_DEL  && k == SM_K_DEL)  return TF_DEL;
    if (SM_K_HOME && k == SM_K_HOME) return TF_HOME;
    if (SM_K_END  && k == SM_K_END)  return TF_END;
    return k;
}

static void menu_set(int on)                   /* every open/close clears the search box */
{
    if (on != menu_open)
        startmenu_reset();
    menu_open = on;
}

static void menu_run(int act, const char *fname)
{
    menu_set(0);

    switch (act) {
    case SM_EXPL:  open_win(WIN_EXPL);  break;
    case SM_CMD:   open_win(WIN_CMD);   break;
    case SM_CALC:  open_win(WIN_CALC);  break;
    case SM_INST:  run_installer();     break;
    case SM_ABOUT: open_win(WIN_ABOUT); break;
    case SM_BIN:   open_win(WIN_BIN);   break;
    case SM_FTFS:  open_win(WIN_FTFS);  break;
    case SM_EXIT:  want_exit = 1;       break;
    case SM_FILE:
        open_win(WIN_EXPL);
#if EXPLORER_OPEN_FILE
        if (fname)
            explorer_open_file(fname);
#else
        (void)fname;
#endif
        break;
    }
}

static void menu_action(int i)
{
    int act = startmenu_act(i);                /* must come first: a search pick is read here */

    menu_run(act, startmenu_file_name());
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

    /* a start menu item drag (reorder) owns the pointer until the button is released */
    if (menu_open && startmenu_pressing()) {
        int x, y, w, h;

        prev = btn;
        if (btn & 1) {
            if (startmenu_drag(mx, my)) {
                startmenu_rect(&x, &y, &w, &h);
                redraw_rect(x, y, w, h);
            } else {
                mouse_cursor_update();
            }
        } else {
            int s = startmenu_release();

            if (s >= 0)
                menu_action(s);
            redraw();
        }
        return;
    }

    for (int i = 0; i < NICONS; i++)
        if (inside(mx, my, icons[i].x - 40, icons[i].y - 8, 128, 86))
            hi = i;

    ho = inside(mx, my, ORB_X, orb_y(), ORB_SZ, ORB_SZ);

    if (menu_open) {
        inmenu = startmenu_hit(mx, my);
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

    if (!busy_on)                                 /* hand over desktop icons and the start orb */
        cursor_set((hi >= 0 || ho) ? CUR_HAND : CUR_ARROW);

    if (pressed) {
        int tb, nx, ny, nw, nh;

        dirty = 1;
        notif_rect(&nx, &ny, &nw, &nh);

        if (menu_open && inmenu != -1) {              /* click inside the panel */
            if (hm >= 0 && !startmenu_press(mx, my))
                menu_action(hm);                      /* places and Exit act on press */
        } else if (ho) {
            menu_set(!menu_open);
        } else if (menu_open) {
            menu_set(0);
        } else if (inside(mx, my, nx, ny, nw, nh)) {  /* notification icon: clears the red dot */
            notif_unread = 0;
        } else if ((tb = wm_tb_hit(mx, my)) >= 0) {   /* taskbar window button */
            wm_tb_click(tb);
        } else {
            wf = wm_pointer(mx, my, btn, oprev);

            if (wf & WM_USED) {
                int s = wm_top();
                struct wm_win *w = (s >= 0) ? wm_get(s) : 0;

                /* The window manager handles focus and caption controls.
                   Calculator and Explorer clicks are then sent to the focused client. */
                if (w && !w->minimized && w->kind == WIN_CALC &&
                    inside(mx, my, w->x, w->y + GLASS_TITLE_H,
                           w->w, w->h - GLASS_TITLE_H)) {
                    if (calc_click(mx, my, w->x, w->y))
                        dirty = 1;
                }
                if (w && !w->minimized && w->kind == WIN_EXPL &&
                    inside(mx, my, w->x, w->y + GLASS_TITLE_H,
                           w->w, w->h - GLASS_TITLE_H)) {
                    if (explorer_click(mx, my, w->x, w->y, w->w, w->h))
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
    xpm_load(&img_hand, (const char *const *)hand_xpm, hand_px, 32 * 32);
    xpm_load(&img_busy[0], (const char *const *)busyf1_xpm, busy_px[0], 32 * 32);
    xpm_load(&img_busy[1], (const char *const *)busyf2_xpm, busy_px[1], 32 * 32);
    xpm_load(&img_busy[2], (const char *const *)busyf3_xpm, busy_px[2], 32 * 32);
    xpm_load(&img_busy[3], (const char *const *)busyf4_xpm, busy_px[3], 32 * 32);
    xpm_load(&img_notif[0], (const char *const *)notif_xpm, notif_px[0], 16 * 16);
    xpm_load(&img_notif[1], (const char *const *)notif1_xpm, notif_px[1], 16 * 16);
    net_load();

    pk = poll_key;
    wm_init();
    calc_reset();
    cmd_reset();
    explorer_reset();

    startmenu_set_file_source(sm_file_count, sm_file_name);
    startmenu_reset();

    menu_open = 0;
    hover_icon = hover_item = -1;
    hover_orb = 0;
    hover_cap = 0;
    hover_cap_slot = -1;
    prev = 0;
    want_exit = 0;
    cur_kind = CUR_ARROW;
    busy_on = 0;
    notif_unread = 0;

    mouse_init();
    mouse_set_cursor(&img_cur, 0, 0);

    clock_read(&clk_h, &clk_m, &clk_d, &clk_mo, &clk_y);
    clk_last = timer_ticks();

    desk_active = 1;
    draw_desktop();
    mouse_cursor_show();

    for (;;) {
        int k;

        if (want_exit)
            break;

        if (mouse_poll())
            pointer_update(mouse_buttons());

        busy_tick();

        if (clock_tick())
            redraw_tray();

        k = poll_key();
        if (k <= 0)
            continue;

        if (menu_open) {
            int r;

            if (k == KEY_WIN) { menu_set(0); redraw(); continue; }

            r = startmenu_key(sm_translate(k));
            if (r == -2) { redraw(); continue; }
            if (r >= 0)  { menu_run(r, startmenu_file_name()); redraw(); continue; }
            if (k == 27) { menu_set(0); redraw(); continue; }

            /* empty box: arrows and Enter still drive the cursor emulation below */
            if (startmenu_searching() ||
                !((k >= 0x80 && k <= 0x83) || k == '\n'))
                continue;
        }

        /* Give the focused calculator first chance to consume keyboard input. */
        {
            int s = wm_top();
            struct wm_win *w = (s >= 0) ? wm_get(s) : 0;

            if (w && !w->minimized && w->kind == WIN_CALC && calc_key(k)) {
                redraw();
                continue;
            }
        }

        /* Focused command prompt gets keys next. */
        {
            int s = wm_top();
            struct wm_win *w = (s >= 0) ? wm_get(s) : 0;

            if (w && !w->minimized && w->kind == WIN_CMD) {
                int r = cmd_key(k);

                if (r == 2) { wm_close(s); redraw(); continue; }
                if (r)      { redraw(); continue; }
            }
        }

        /* Focused Fish Explorer gets keys before arrows move the cursor. */
        {
            int s = wm_top();
            struct wm_win *w = (s >= 0) ? wm_get(s) : 0;

            if (w && !w->minimized && w->kind == WIN_EXPL && explorer_key(k)) {
                redraw();
                continue;
            }
        }

        if (k == 'q' || k == 'Q')
            break;

        if (k == 27) {
            if (menu_open) {
                menu_set(0);
                redraw();
                continue;
            }
            break;
        }

        if (k == 0x80)      { mouse_move_by(0, -12);  pointer_update(0); }
        else if (k == 0x81) { mouse_move_by(0,  12);  pointer_update(0); }
        else if (k == 0x82) { mouse_move_by(-12, 0);  pointer_update(0); }
        else if (k == 0x83) { mouse_move_by( 12, 0);  pointer_update(0); }
        else if (k == '\n' || k == ' ') { pointer_update(1); pointer_update(0); }

          if (k == KEY_WIN || k == 'm' || k == 'M') {
            menu_set(!menu_open);
            hover_item = -1;
            redraw();
        }
        else if (k == 'a' || k == 'A') {
            open_win(WIN_ABOUT);
            redraw();
        }
        else if (k == 'd' || k == 'D') {
            wm_close(wm_top());
            menu_set(0);
            redraw();
        }
		
#if DESK_DEBUG_KEYS
        else if (k == 'b' || k == 'B') { if (busy_on) busy_end(); else busy_begin(); }
        else if (k == 'n' || k == 'N') { desktop_notify(); }
        else if (k == 'w' || k == 'W') { net_state = (net_state + 1) % NET_N; redraw_tray(); }
#endif
    }

    desk_active = 0;
    busy_end();
    mouse_cursor_hide();
}
