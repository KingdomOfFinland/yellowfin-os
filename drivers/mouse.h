#ifndef MOUSE_H
#define MOUSE_H

#include "graphics.h"

void mouse_init(void);
int  mouse_poll(void);
int  mouse_x(void);
int  mouse_y(void);
int  mouse_buttons(void);
void mouse_move_by(int dx, int dy);
void mouse_diag(void);
void mouse_probe(void);
void mouse_set_cursor(const gfx_image *im, int hotx, int hoty);
void mouse_cursor_show(void);
void mouse_cursor_hide(void);
void mouse_cursor_update(void);

#endif