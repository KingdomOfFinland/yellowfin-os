/* startmenu.c - Windows 7-style start menu for Yellowfin OS.
   Left: light program list + search box. Right: glass column with avatar, places, exit button. */

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

int startmenu_hit(int mx, int my)
{
    if (!inside(mx, my, MENU_X, menu_y(), MENU_W, MENU_H))
        return -1;

    for (int i = 0; i < N_ITEMS; i++) {
        int x, y, w, h;

        item_rect(i, &x, &y, &w, &h);
        if (inside(mx, my, x, y, w, h))
            return i;
    }
    return -2;
}

int startmenu_act(int idx)
{
    return (idx >= 0 && idx < N_ITEMS) ? items[idx].act : -1;
}

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

void startmenu_draw(int hover, gfx_image *logo)
{
    int my = menu_y(), rx = MENU_X + LEFT_W, i;

    /* shadow, glass body, outline */
    gfx_blend_rect(MENU_X + 5, my + 5, MENU_W, MENU_H, 0x000000, 100);
    gfx_blend_rect(MENU_X, my, MENU_W, MENU_H, 0x7FA6D2, 200);
    gfx_frame(MENU_X, my, MENU_W, MENU_H, 0xFFFFFF);
    gfx_blend_rect(MENU_X + 1, my + 1, MENU_W - 2, 18, 0xFFFFFF, 40);   /* top sheen */

    /* left panel */
    gfx_rect(MENU_X + 4, my + 4, LEFT_W - 8, MENU_H - 8, 0xF4F8FC);
    gfx_frame(MENU_X + 4, my + 4, LEFT_W - 8, MENU_H - 8, 0x9FB6CE);

    for (i = 0; i < N_LEFT; i++) {
        int x, y, w, h;

        item_rect(i, &x, &y, &w, &h);
        if (i == hover) {
            gfx_blend_rect(x, y, w, h, 0xA0C8F0, 140);
            gfx_frame(x, y, w, h, 0x7FA6D2);
        }
        draw_micon(items[i].act, x + 8, y + (h - 28) / 2, logo);
        gfx_text(x + 46, y + (h - 16) / 2, items[i].label, 0x14283E);
    }

    /* separator + search box (cosmetic for now) */
    {
        int sy = my + 6 + N_LEFT * LI_H + 8;

        gfx_rect(MENU_X + 14, sy, LEFT_W - 28, 1, 0xC8D4E4);
        gfx_rect(MENU_X + 12, sy + 10, LEFT_W - 24, 26, 0xFFFFFF);
        gfx_frame(MENU_X + 12, sy + 10, LEFT_W - 24, 26, 0x9FB6CE);
        gfx_text(MENU_X + 20, sy + 15, "Search programs and files", 0x8FA3B8);
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