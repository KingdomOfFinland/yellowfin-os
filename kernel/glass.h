#ifndef GLASS_H
#define GLASS_H

#include "graphics.h"

#define GLASS_TASKBAR_H 40
#define GLASS_TITLE_H   30
#define GLASS_CLOSE_W   44
#define GLASS_CLOSE_H   20
#define GLASS_CLOSE_X(wx, ww) ((wx) + (ww) - GLASS_CLOSE_W - 6)
#define GLASS_CLOSE_Y(wy)     ((wy) + 5)
#define GLASS_CAP_W 28

extern int glass_hover_btn;

void glass_wallpaper(void);
void glass_panel(int x, int y, int w, int h);
void glass_window(int x, int y, int w, int h, const char *title);
void glass_taskbar(void);
void glass_highlight(int x, int y, int w, int h);
void glass_caption(int x, int y, int w);

#endif