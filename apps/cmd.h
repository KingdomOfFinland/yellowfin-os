#ifndef CMD_APP_H
#define CMD_APP_H
#include "glass.h"

#define CMD_COLS 66
#define CMD_ROWS 15
#define CMD_W    (CMD_COLS * 8 + 16)
#define CMD_H    (GLASS_TITLE_H + CMD_ROWS * 16 + 16)

void cmd_reset(void);
void cmd_draw(int x, int y);   /* x,y = window origin */
int  cmd_key(int k);           /* 0 = not consumed, 1 = consumed (redraw), 2 = close window */

#endif