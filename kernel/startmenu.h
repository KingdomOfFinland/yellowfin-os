#ifndef STARTMENU_H
#define STARTMENU_H

#include "graphics.h"

/* actions returned by startmenu_act() */
enum { SM_EXPL, SM_CMD, SM_CALC, SM_INST, SM_ABOUT, SM_BIN, SM_FTFS, SM_EXIT };

/* hover = item index under the pointer or -1; logo = the fish logo image (used as the avatar) */
void startmenu_draw(int hover, gfx_image *logo);

/* -1 outside the panel, -2 inside but not on an item, else item index */
int  startmenu_hit(int mx, int my);

/* index from startmenu_hit -> SM_* action */
int  startmenu_act(int idx);

#endif