/* startmenu.c - Windows 7-style start menu for Yellowfin OS.
   Left: light program list + search box. Right: glass column with avatar, places, exit button.
   Left-list items can be reordered by dragging.
   Typing filters apps and FTFS files; results replace the program list. */

#include "startmenu.h"
#include "glass.h"

#define MENU_X   4
#define MENU_W   372
#define MENU_H   264
#define LEFT_W   228
#define RIGHT_W  (MENU_W - LEFT_W)
#define LI_H     40
#define RI_H     30
#define N_LEFT   5
#define N_ITEMS  9
#define EXIT_IDX 8
#define DRAG_THRESHOLD 5

#define RES_MAX  7
#define RES_H    28

struct mitem { const char *label; int act; };

static const struct mitem items[N_ITEMS] = {
    /* left: programs */
    { "Fish Explorer",    SM_EXPL  },
    { "Command Prompt",   SM_CMD   },
    { "Calculator",       SM_CALC  },
    { "Install Yellowfin", SM_INST },
    { "About Yellowfin",  SM_ABOUT },
    /* right: places */
    { "Documents",        SM_EXPL  },
    { "Computer",         SM_FTFS  },
    { "Recycle Bin",      SM_BIN   },
    /* right: bottom button */
    { "Exit to shell",    SM_EXIT  },
};

/* drag-to-reorder state: order[] maps a display slot to an items[] index */
static int order[N_LEFT] = { 0, 1, 2, 3, 4 };
static int press_slot = -1, drag_cur, moved, press_x, press_y, grab_dy, last_my;

/* ---- search state ---- */
struct result { int act; int item; int file; };     /* item: items[] index or -1; file: file slot or -1 */

static textfield q;
static struct result res[RES_MAX];
static int n_res, res_sel, caret_on = 1, picked_file = -1;
static int (*file_count)(void);
static const char *(*file_name)(int);

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int menu_y(void)
{
    return (int)gfx_h() - GLASS_TASKBAR_H - MENU_H - 2;
}

static void item_rect(int i, int *x, int *y, int *w, int *h)
{
    int my = menu_y();

    if (i < N_LEFT) {
        *x = MENU_X + 6;
        *y = my + 6 + i * LI_H;
        *w = LEFT_W - 12;
        *h = LI_H;
    } else if (i < EXIT_IDX) {
        *x = MENU_X + LEFT_W;
        *y = my + 92 + (i - N_LEFT) * RI_H;
        *w = RIGHT_W;
        *h = RI_H;
    } else {
        *x = MENU_X + LEFT_W + 12;
        *y = my + MENU_H - 40;
        *w = RIGHT_W - 24;
        *h = 28;
    }
}

static void res_rect(int r, int *x, int *y, int *w, int *h)
{
    *x = MENU_X + 6;
    *y = menu_y() + 6 + r * RES_H;
    *w = LEFT_W - 12;
    *h = RES_H;
}

/* ---- search logic ---- */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* case-insensitive substring test */
static int ci_has(const char *hay, const char *needle)
{
    int i, j;

    if (!needle[0])
        return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j]; j++)
            if (lower(hay[i + j]) != lower(needle[j]))
                break;
        if (!needle[j])
            return 1;
    }
    return 0;
}

static void refilter(void)
{
    int s, i, n;

    n_res = 0;
    res_sel = 0;
    if (!q.len)
        return;

    /* apps first, in the current (dragged) order */
    for (s = 0; s < N_LEFT && n_res < RES_MAX; s++) {
        int it = order[s];

        if (ci_has(items[it].label, q.buf)) {
            res[n_res].act = items[it].act;
            res[n_res].item = it;
            res[n_res].file = -1;
            n_res++;
        }
    }

    /* then FTFS files */
    if (file_count && file_name) {
        n = file_count();
        for (i = 0; i < n && n_res < RES_MAX; i++) {
            const char *nm = file_name(i);

            if (nm && nm[0] && ci_has(nm, q.buf)) {
                res[n_res].act = SM_FILE;
                res[n_res].item = -1;
                res[n_res].file = i;
                n_res++;
            }
        }
    }
}

void startmenu_set_file_source(int (*count)(void), const char *(*name)(int i))
{
    file_count = count;
    file_name = name;
}

void startmenu_reset(void)
{
    tf_clear(&q);
    n_res = 0;
    res_sel = 0;
    picked_file = -1;
    press_slot = -1;
    moved = 0;
    caret_on = 1;
}

static int pick(int r)
{
    if (r < 0 || r >= n_res)
        return -1;
    picked_file = res[r].file;
    return res[r].act;
}

int startmenu_key(int key)
{
    if (key == '\n' || key == '\r')
        return n_res ? pick(res_sel) : -1;

    if (key == 27) {                                /* Esc clears the query first */
        if (!q.len)
            return -1;
        tf_clear(&q);
        refilter();
        return -2;
    }

    if (key == SM_KEY_UP) {
        if (n_res && res_sel > 0) { res_sel--; return -2; }
        return -1;
    }
    if (key == SM_KEY_DOWN) {
        if (n_res && res_sel < n_res - 1) { res_sel++; return -2; }
        return -1;
    }

    if (tf_key(&q, key)) {
        refilter();
        caret_on = 1;
        return -2;
    }
    return -1;
}

const char *startmenu_file_name(void)
{
    if (picked_file >= 0 && file_name)
        return file_name(picked_file);
    return 0;
}

int startmenu_searching(void)
{
    return q.len > 0;
}

void startmenu_caret(int on)
{
    caret_on = on;
}

/* ---- hit testing ---- */

int startmenu_hit(int mx, int my)
{
    int i;

    if (!inside(mx, my, MENU_X, menu_y(), MENU_W, MENU_H))
        return -1;

    if (q.len) {                                    /* results replace the left list */
        for (i = 0; i < n_res; i++) {
            int x, y, w, h;

            res_rect(i, &x, &y, &w, &h);
            if (inside(mx, my, x, y, w, h))
                return SM_SEARCH_BASE + i;
        }
    }

    for (i = q.len ? N_LEFT : 0; i < N_ITEMS; i++) {
        int x, y, w, h;

        item_rect(i, &x, &y, &w, &h);
        if (inside(mx, my, x, y, w, h))
            return i;
    }
    return -2;
}

int startmenu_act(int idx)
{
    if (idx >= SM_SEARCH_BASE)
        return pick(idx - SM_SEARCH_BASE);
    if (idx < 0 || idx >= N_ITEMS)
        return -1;
    if (idx < N_LEFT)
        idx = order[idx];
    return items[idx].act;
}

/* ---- drag to reorder ---- */

/* returns 1 if the press landed on a left-list item (launch waits for release) */
int startmenu_press(int mx, int my)
{
    if (q.len)                                      /* no dragging while searching */
        return 0;

    for (int s = 0; s < N_LEFT; s++) {
        int x, y, w, h;

        item_rect(s, &x, &y, &w, &h);
        if (inside(mx, my, x, y, w, h)) {
            press_slot = drag_cur = s;
            press_x = mx;
            press_y = my;
            grab_dy = my - y;
            last_my = my;
            moved = 0;
            return 1;
        }
    }
    return 0;
}

int startmenu_pressing(void) { return press_slot >= 0; }

/* returns 1 if the menu needs a redraw */
int startmenu_drag(int mx, int my)
{
    int target, tmp, i;

    if (press_slot < 0)
        return 0;

    if (!moved) {                                   /* small wobble still counts as a click */
        int dx = mx - press_x, dy = my - press_y;

        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD)
            return 0;
        moved = 1;
    }
    last_my = my;

    target = (my < menu_y() + 6) ? 0 : (my - (menu_y() + 6)) / LI_H;
    if (target > N_LEFT - 1)
        target = N_LEFT - 1;

    if (target != drag_cur) {                       /* live reorder: neighbours slide over */
        tmp = order[drag_cur];
        if (target > drag_cur)
            for (i = drag_cur; i < target; i++) order[i] = order[i + 1];
        else
            for (i = drag_cur; i > target; i--) order[i] = order[i - 1];
        order[target] = tmp;
        drag_cur = target;
    }
    return 1;
}

/* returns the slot to launch if it was a plain click, else -1 */
int startmenu_release(void)
{
    int s = press_slot, was_moved = moved;

    press_slot = -1;
    moved = 0;
    return (s >= 0 && !was_moved) ? s : -1;
}

void startmenu_rect(int *x, int *y, int *w, int *h)
{
    *x = MENU_X;
    *y = menu_y();
    *w = MENU_W + 6;                                /* +6 covers the drop shadow */
    *h = MENU_H + 6;
}

/* ---- drawing ---- */

static void shadow_text(int x, int y, const char *s, uint32_t fg)
{
    gfx_text(x + 1, y + 1, s, 0x0A1424);
    gfx_text(x, y, s, fg);
}

/* 28x28 program icons built from rectangles (swap for XPMs later) */
static void draw_micon(int act, int x, int y, gfx_image *logo)
{
    if (act == SM_EXPL) {                              /* folder */
        gfx_rect(x + 2, y + 5, 11, 5, 0xE0B03A);
        gfx_rect(x + 2, y + 8, 24, 16, 0xF2C94C);
        gfx_frame(x + 2, y + 8, 24, 16, 0xB8892A);
        gfx_rect(x + 4, y + 11, 20, 1, 0xFFE9A0);
    } else if (act == SM_CMD) {                        /* terminal */
        gfx_rect(x + 2, y + 4, 24, 20, 0x101820);
        gfx_frame(x + 2, y + 4, 24, 20, 0x9FB6CE);
        gfx_rect(x + 2, y + 4, 24, 3, 0x5A7FA8);
        gfx_text(x + 6, y + 6, ">_", 0xC8D4E4);
    } else if (act == SM_CALC) {                       /* calculator */
        gfx_rect(x + 5, y + 2, 18, 24, 0xC8D4E4);
        gfx_frame(x + 5, y + 2, 18, 24, 0x5A7FA8);
        gfx_rect(x + 7, y + 4, 14, 5, 0x9ED89E);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                gfx_rect(x + 8 + c * 5, y + 12 + r * 5, 3, 3, 0x5A7FA8);
    } else if (act == SM_INST) {                       /* installer = fish logo */
        gfx_blit_scaled(logo, x, y, 28, 28);
    } else if (act == SM_ABOUT) {                      /* info badge */
        gfx_rect(x + 3, y + 3, 22, 22, 0x3A78C8);
        gfx_frame(x + 3, y + 3, 22, 22, 0xFFFFFF);
        gfx_text(x + 12, y + 5, "i", 0xFFFFFF);
    }
}

/* little page icon for file results */
static void draw_ficon(int x, int y)
{
    gfx_rect(x + 6, y + 3, 16, 22, 0xFFFFFF);
    gfx_frame(x + 6, y + 3, 16, 22, 0x5A7FA8);
    gfx_rect(x + 9, y + 9, 10, 1, 0x9FB6CE);
    gfx_rect(x + 9, y + 13, 10, 1, 0x9FB6CE);
    gfx_rect(x + 9, y + 17, 7, 1, 0x9FB6CE);
}

static void draw_results(int hover, gfx_image *logo)
{
    int r;

    if (!n_res) {
        gfx_text(MENU_X + 20, menu_y() + 16, "No matches found", 0x5A6F86);
        return;
    }

    for (r = 0; r < n_res; r++) {
        int x, y, w, h, hov = (hover == SM_SEARCH_BASE + r);
        const char *label;

        res_rect(r, &x, &y, &w, &h);
        if (hov || r == res_sel) {
            gfx_blend_rect(x, y, w, h, 0xA0C8F0, hov ? 140 : 100);
            gfx_frame(x, y, w, h, 0x7FA6D2);
        }

        if (res[r].file < 0) {
            draw_micon(res[r].act, x + 8, y, logo);
            label = items[res[r].item].label;
        } else {
            draw_ficon(x + 8, y);
            label = file_name(res[r].file);
        }
        gfx_text(x + 46, y + (h - 16) / 2, label ? label : "", 0x14283E);
    }
}

void startmenu_draw(int hover, gfx_image *logo)
{
    int my = menu_y(), rx = MENU_X + LEFT_W, i;
    int dragging = (press_slot >= 0 && moved);

    /* shadow, glass body, outline */
    gfx_blend_rect(MENU_X + 5, my + 5, MENU_W, MENU_H, 0x000000, 100);
    gfx_blend_rect(MENU_X, my, MENU_W, MENU_H, 0x7FA6D2, 200);
    gfx_frame(MENU_X, my, MENU_W, MENU_H, 0xFFFFFF);
    gfx_blend_rect(MENU_X + 1, my + 1, MENU_W - 2, 18, 0xFFFFFF, 40);   /* top sheen */

    /* left panel */
    gfx_rect(MENU_X + 4, my + 4, LEFT_W - 8, MENU_H - 8, 0xF4F8FC);
    gfx_frame(MENU_X + 4, my + 4, LEFT_W - 8, MENU_H - 8, 0x9FB6CE);

    if (q.len) {
        draw_results(hover, logo);
    } else {
        for (i = 0; i < N_LEFT; i++) {
            const struct mitem *it = &items[order[i]];
            int x, y, w, h;

            item_rect(i, &x, &y, &w, &h);
            if (dragging && i == drag_cur) {        /* empty slot where the item will land */
                gfx_blend_rect(x, y, w, h, 0xA0C8F0, 50);
                continue;
            }
            if (i == hover && !dragging) {
                gfx_blend_rect(x, y, w, h, 0xA0C8F0, 140);
                gfx_frame(x, y, w, h, 0x7FA6D2);
            }
            draw_micon(it->act, x + 8, y + (h - 28) / 2, logo);
            gfx_text(x + 46, y + (h - 16) / 2, it->label, 0x14283E);
        }

        if (dragging) {                             /* the lifted item follows the pointer */
            const struct mitem *it = &items[order[drag_cur]];
            int x, y, w, h, fy, lo = my + 6, hi = my + 6 + (N_LEFT - 1) * LI_H;

            item_rect(drag_cur, &x, &y, &w, &h);
            fy = last_my - grab_dy;
            if (fy < lo) fy = lo;
            if (fy > hi) fy = hi;
            gfx_blend_rect(x, fy, w, h, 0xA0C8F0, 190);
            gfx_frame(x, fy, w, h, 0x7FA6D2);
            draw_micon(it->act, x + 8, fy + (h - 28) / 2, logo);
            gfx_text(x + 46, fy + (h - 16) / 2, it->label, 0x14283E);
        }
    }

    /* separator + search box */
    {
        int sy = my + 6 + N_LEFT * LI_H + 8;

        gfx_rect(MENU_X + 14, sy, LEFT_W - 28, 1, 0xC8D4E4);
        tf_draw(&q, MENU_X + 12, sy + 10, LEFT_W - 24, 26,
                "Search programs and files", caret_on);
    }

    /* right column: avatar + name */
    {
        int ax = rx + (RIGHT_W - 48) / 2, ay = my + 12;
        int nw = gfx_text_width("Yellowfin");

        gfx_rect(ax - 2, ay - 2, 52, 52, 0xFFFFFF);
        gfx_blit_scaled(logo, ax, ay, 48, 48);
        shadow_text(rx + (RIGHT_W - nw) / 2, ay + 56, "Yellowfin", 0xFFFFFF);
    }

    /* right column: places */
    for (i = N_LEFT; i < EXIT_IDX; i++) {
        int x, y, w, h;

        item_rect(i, &x, &y, &w, &h);
        if (i == hover)
            glass_highlight(x + 2, y, w - 4, h);
        shadow_text(x + 16, y + (h - 16) / 2, items[i].label, 0xFFFFFF);
    }

    /* exit button */
    {
        int x, y, w, h;

        item_rect(EXIT_IDX, &x, &y, &w, &h);
        gfx_blend_rect(x, y, w, h, 0x24406A, 200);
        gfx_frame(x, y, w, h, 0xD8E8F8);
        if (hover == EXIT_IDX)
            glass_highlight(x + 1, y + 1, w - 2, h - 2);
        shadow_text(x + (w - gfx_text_width(items[EXIT_IDX].label)) / 2,
                    y + (h - 16) / 2, items[EXIT_IDX].label, 0xFFFFFF);
    }
}
