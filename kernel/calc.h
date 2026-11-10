#ifndef YF_CALC_H
#define YF_CALC_H

#include "glass.h"            /* for GLASS_TITLE_H */

/* size to open the window with: wm_open(WIN_CALC, "Calculator", CALC_W, CALC_H) */
#define CALC_W 250
#define CALC_H (GLASS_TITLE_H + 274)

void calc_reset(void);
void calc_draw(int x, int y);                       /* x,y = window top-left */
int  calc_click(int mx, int my, int x, int y);      /* returns 1 if a button was pressed */
int  calc_key(int k);                               /* returns 1 if the key was used */

#endif