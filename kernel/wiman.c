#include "wiman.h"
#include "glass.h"
#include "dirty.h"

enum { WM_HIT_NONE, WM_HIT_CLIENT, WM_HIT_TITLE, WM_HIT_MIN, WM_HIT_MAX, WM_HIT_CLOSE };

static struct wm_win wins[WM_MAX];
static int zord[WM_MAX], nz;
static int drag = -1, drag_dx, drag_dy;

#define WIN_SHADOW 8

static void dirty_win(const struct wm_win *w)
{
    dirty_add(w->x - 2, w->y - 2, w->w + WIN_SHADOW + 4, w->h + WIN_SHADOW + 4);
}

static void dirty_title(int slot)        /* focused/unfocused look changes */
{
    if (slot >= 0)
        dirty_add(wins[slot].x - 2, wins[slot].y - 2,
                  wins[slot].w + 4, GLASS_TITLE_H + 4);
}

static void dirty_taskbar(void)
{
    dirty_add(0, (int)gfx_h() - GLASS_TASKBAR_H, (int)gfx_w(), GLASS_TASKBAR_H);
}

/* TEMPORARY bridge so the old deskt.c still builds: returns the bounding box
   of everything marked dirty and clears the list. Remove once deskt.c uses
   dirty_count()/dirty_get() directly. */
void wm_dirty_rect(int *x, int *y, int *w, int *h)
{
    int n = dirty_count(), x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    for (int i = 0; i < n; i++) {
        int rx, ry, rw, rh;
        dirty_get(i, &rx, &ry, &rw, &rh);
        if (i == 0 || rx < x0)       x0 = rx;
        if (i == 0 || ry < y0)       y0 = ry;
        if (i == 0 || rx + rw > x1)  x1 = rx + rw;
        if (i == 0 || ry + rh > y1)  y1 = ry + rh;
    }
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    dirty_clear();
}

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

void wm_init(void)
{
    for (int i = 0; i < WM_MAX; i++)
        wins[i].used = 0;
    nz = 0;
    drag = -1;
}

struct wm_win *wm_get(int slot) { return &wins[slot]; }
int wm_nz(void)                 { return nz; }
int wm_z(int n)                 { return zord[n]; }
int wm_dragging(void)           { return drag >= 0; }

int wm_top(void)
{
    for (int n = nz - 1; n >= 0; n--)
        if (!wins[zord[n]].minimized)
            return zord[n];
    return -1;
}

void wm_raise(int slot)
{
    int i, old = wm_top();

    for (i = 0; i < nz && zord[i] != slot; i++)
        ;
    if (i == nz)
        return;
    for (; i < nz - 1; i++)
        zord[i] = zord[i + 1];
    zord[nz - 1] = slot;

    if (old != slot) {
        dirty_title(old);
        dirty_win(&wins[slot]);
        dirty_taskbar();
    }
}

int wm_open(int kind, const char *title, int w, int h)
{
    int i, n, old;
    int sw = (int)gfx_w();
    int sh = (int)gfx_h() - GLASS_TASKBAR_H;
    struct wm_win *p;

    for (i = 0; i < WM_MAX; i++) {
        if (wins[i].used && wins[i].kind == kind) {
            wins[i].minimized = 0;
            dirty_win(&wins[i]);
            wm_raise(i);
            dirty_taskbar();
            return i;
        }
    }

    for (i = 0; i < WM_MAX && wins[i].used; i++)
        ;
    if (i == WM_MAX)
        return -1;

    if (w > sw - 20) w = sw - 20;
    if (h > sh - 20) h = sh - 20;

    n = nz % 5;                                  /* cascade a little */
    p = &wins[i];
    p->used = 1;
    p->kind = kind;
    p->title = title;
    p->w = w;
    p->h = h;
    p->x = (sw - w) / 2 + (n - 2) * 28;
    p->y = (sh - h) / 2 + (n - 2) * 28;
    if (p->x < 0) { p->x = 0; }
    if (p->y < 0) { p->y = 0; }
    p->rx = p->x; p->ry = p->y; p->rw = p->w; p->rh = p->h;
    p->minimized = 0;
    p->maximized = 0;

    old = wm_top();
    zord[nz++] = i;

    dirty_title(old);
    dirty_win(p);
    dirty_taskbar();
    return i;
}

void wm_close(int slot)
{
    int i, j;

    if (slot < 0 || slot >= WM_MAX || !wins[slot].used)
        return;

    dirty_win(&wins[slot]);                      /* before it disappears */

    wins[slot].used = 0;
    for (i = 0, j = 0; i < nz; i++)
        if (zord[i] != slot)
            zord[j++] = zord[i];
    nz = j;

    if (drag == slot)
        drag = -1;

    dirty_title(wm_top());                       /* new top window gets focus look */
    dirty_taskbar();
}

static void toggle_max(struct wm_win *w)
{
    dirty_win(w);                                /* old position */
    if (!w->maximized) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        w->x = 0;
        w->y = 0;
        w->w = (int)gfx_w();
        w->h = (int)gfx_h() - GLASS_TASKBAR_H;
        w->maximized = 1;
    } else {
        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
        w->maximized = 0;
    }
    dirty_win(w);                                /* new position */
}

/* topmost window under the pointer; returns which part was hit */
static int hit(int mx, int my, int *slot)
{
    for (int n = nz - 1; n >= 0; n--) {
        int s = zord[n], bx, by;
        struct wm_win *w = &wins[s];

        if (w->minimized || !inside(mx, my, w->x, w->y, w->w, w->h))
            continue;

        *slot = s;
        bx = GLASS_CLOSE_X(w->x, w->w);
        by = GLASS_CLOSE_Y(w->y);

        if (inside(mx, my, bx, by, GLASS_CLOSE_W, GLASS_CLOSE_H))
            return WM_HIT_CLOSE;
        if (inside(mx, my, bx - GLASS_CAP_W, by, GLASS_CAP_W, GLASS_CLOSE_H))
            return WM_HIT_MAX;
        if (inside(mx, my, bx - 2 * GLASS_CAP_W, by, GLASS_CAP_W, GLASS_CLOSE_H))
            return WM_HIT_MIN;
        if (my < w->y + GLASS_TITLE_H)
            return WM_HIT_TITLE;
        return WM_HIT_CLIENT;
    }
    return WM_HIT_NONE;
}

int wm_cap_at(int mx, int my, int *slot)
{
    int s = -1, part = hit(mx, my, &s);

    *slot = s;
    if (part == WM_HIT_MIN)   return 1;
    if (part == WM_HIT_MAX)   return 2;
    if (part == WM_HIT_CLOSE) return 3;
    return 0;
}

/* rectangle of one caption button: cap 1 = min, 2 = max, 3 = close */
void wm_cap_rect(int slot, int cap, int *x, int *y, int *w, int *h)
{
    struct wm_win *p = &wins[slot];
    int bx = GLASS_CLOSE_X(p->x, p->w);

    *y = GLASS_CLOSE_Y(p->y);
    *h = GLASS_CLOSE_H;
    *w = (cap == 3) ? GLASS_CLOSE_W : GLASS_CAP_W;
    *x = (cap == 3) ? bx : bx - (cap == 2 ? 1 : 2) * GLASS_CAP_W;
}

int wm_pointer(int mx, int my, int btn, int prev_btn)
{
    int pressed = (btn & 1) && !(prev_btn & 1);
    int part, slot = -1;
    int sw = (int)gfx_w(), sh = (int)gfx_h();

    if (drag >= 0) {                              /* a drag is in progress */
        struct wm_win *w = &wins[drag];

        if (btn & 1) {
            int nx = mx - drag_dx, ny = my - drag_dy;

            if (nx < 40 - w->w) nx = 40 - w->w;   /* keep part of the titlebar reachable */
            if (nx > sw - 40)   nx = sw - 40;
            if (ny < 0)         ny = 0;
            if (ny > sh - GLASS_TASKBAR_H - GLASS_TITLE_H)
                ny = sh - GLASS_TASKBAR_H - GLASS_TITLE_H;

            if (nx != w->x || ny != w->y) {
                dirty_win(w);                     /* old rect */
                w->x = nx;
                w->y = ny;
                dirty_win(w);                     /* new rect */
                return WM_USED | WM_REDRAW;
            }
            return WM_USED;
        }
        drag = -1;                                /* button released */
        return WM_USED;
    }

    if (!pressed)
        return 0;

    part = hit(mx, my, &slot);
    if (part == WM_HIT_NONE)
        return 0;

    wm_raise(slot);

    if (part == WM_HIT_CLOSE) {
        wm_close(slot);
    } else if (part == WM_HIT_MAX) {
        toggle_max(&wins[slot]);
    } else if (part == WM_HIT_MIN) {
        dirty_win(&wins[slot]);
        wins[slot].minimized = 1;
        dirty_title(wm_top());
        dirty_taskbar();
    } else if (part == WM_HIT_TITLE && !wins[slot].maximized) {
        drag = slot;
        drag_dx = mx - wins[slot].x;
        drag_dy = my - wins[slot].y;
    }

    return WM_USED | WM_REDRAW;
}

/* ---- taskbar buttons ---- */

int wm_ntb(void)
{
    int n = 0;

    for (int i = 0; i < WM_MAX; i++)
        if (wins[i].used)
            n++;
    return n;
}

int wm_tb_slot(int n)
{
    for (int i = 0; i < WM_MAX; i++) {
        if (wins[i].used) {
            if (n == 0)
                return i;
            n--;
        }
    }
    return -1;
}

void wm_tb_rect(int n, int *x, int *y, int *w, int *h)
{
    *x = WM_TB_X0 + n * (WM_TB_W + WM_TB_GAP);
    *y = (int)gfx_h() - GLASS_TASKBAR_H + 4;
    *w = WM_TB_W;
    *h = GLASS_TASKBAR_H - 8;
}

int wm_tb_hit(int mx, int my)
{
    int n = wm_ntb();

    for (int i = 0; i < n; i++) {
        int x, y, w, h;

        wm_tb_rect(i, &x, &y, &w, &h);
        if (inside(mx, my, x, y, w, h))
            return wm_tb_slot(i);
    }
    return -1;
}

void wm_tb_click(int slot)
{
    if (slot < 0 || !wins[slot].used)
        return;

    if (wins[slot].minimized) {
        wins[slot].minimized = 0;
        dirty_win(&wins[slot]);
        wm_raise(slot);
        dirty_taskbar();
    } else if (slot == wm_top()) {
        dirty_win(&wins[slot]);
        wins[slot].minimized = 1;
        dirty_title(wm_top());
        dirty_taskbar();
    } else {
        wm_raise(slot);
    }
}