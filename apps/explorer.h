#ifndef EXPLORER_H
#define EXPLORER_H

#include "ftfs.h"      /* u8/u16/u32 */

#define EX_W 520
#define EX_H 360

void explorer_reset(void);
void explorer_draw(int x, int y, int w, int h);

/* mx,my = pointer; x,y,w,h = window rect. Returns 1 if a redraw is needed. */
int  explorer_click(int mx, int my, int x, int y, int w, int h);

/* Returns 1 if the key was consumed (caller should redraw). */
int  explorer_key(int k);

#endif