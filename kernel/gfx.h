
#ifndef YELLOWFIN_GFX_H
#define YELLOWFIN_GFX_H

#include "types.h"

int gfx_init(void);
void gfx_clear(u32 color);
void gfx_putpixel(u32 x, u32 y, u32 color);
void gfx_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

#endif
