#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

typedef struct { int w, h; uint32_t *px; } gfx_image;   /* 0xAARRGGBB, A=0 -> transparent */

int      gfx_init(void);            /* 1 = ok, 0 = no graphics mode */
uint32_t gfx_w(void);
uint32_t gfx_h(void);

void gfx_flip(void);                                  /* whole back buffer to screen */
void gfx_flip_rect(int x, int y, int w, int h);       /* only part of it */
void gfx_capture(void);                               /* copy the real screen into the back buffer */

void gfx_clear(uint32_t c);
void gfx_rect(int x, int y, int w, int h, uint32_t c);
void gfx_frame(int x, int y, int w, int h, uint32_t c);          /* 1px outline */
void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_blend_rect(int x, int y, int w, int h, uint32_t c, uint32_t alpha); /* alpha 0..255 */
void gfx_text(int x, int y, const char *s, uint32_t fg);

void gfx_blit(const gfx_image *im, int x, int y);
void gfx_blit_scaled(const gfx_image *im, int x, int y, int w, int h);
void gfx_getrect(int x, int y, int w, int h, uint32_t *buf);        /* save back buffer pixels */
void gfx_putrect(int x, int y, int w, int h, const uint32_t *buf);  /* restore them */

int  xpm_load(gfx_image *out, const char *const *xpm, uint32_t *buf, int cap);

#endif