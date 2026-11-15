#ifndef WIMAN_H
#define WIMAN_H

#define WM_MAX 8

/* return flags from wm_pointer() */
#define WM_REDRAW 1
#define WM_USED   2

/* taskbar button layout (first button sits just right of the start orb) */
#define WM_TB_X0  60
#define WM_TB_W   160
#define WM_TB_GAP 4

struct wm_win {
    int used, kind;
    int x, y, w, h;
    int rx, ry, rw, rh;          /* saved rect for un-maximize */
    int minimized, maximized;
    const char *title;
};

void wm_init(void);

/* open a window of this kind, or raise it if it is already open; returns slot or -1 */
int  wm_open(int kind, const char *title, int w, int h);
void wm_close(int slot);
void wm_raise(int slot);

int  wm_top(void);               /* focused slot, or -1 */
int  wm_nz(void);                /* number of windows in z-order */
int  wm_z(int n);                /* slot at z position n (0 = back) */
struct wm_win *wm_get(int slot);

int  wm_dragging(void);
void wm_dirty_rect(int *x, int *y, int *w, int *h);   /* screen area changed by the last drag step */
int  wm_cap_at(int mx, int my, int *slot);   /* caption button under pointer: 0 none, 1 min, 2 max, 3 close */
int  wm_pointer(int mx, int my, int btn, int prev_btn);

/* taskbar buttons, one per open window, in slot order */
int  wm_ntb(void);
int  wm_tb_slot(int n);
void wm_tb_rect(int n, int *x, int *y, int *w, int *h);
int  wm_tb_hit(int mx, int my);  /* slot under the pointer, or -1 */
void wm_tb_click(int slot);
void wm_cap_rect(int slot, int cap, int *x, int *y, int *w, int *h);

#endif