#include "graphics.h"

#define VBE_OK    (*(volatile uint8_t  *)0x8FF0)
#define VBE_PITCH (*(volatile uint16_t *)0x9010)
#define VBE_W     (*(volatile uint16_t *)0x9012)
#define VBE_H     (*(volatile uint16_t *)0x9014)
#define VBE_BPP   (*(volatile uint8_t  *)0x9019)
#define VBE_FB    (*(volatile uint32_t *)0x9028)
#define FONT      ((const uint8_t *)0x9400)

#define BACKBUF_ADDR 0x00400000u   /* 4 MiB: free RAM above the kernel */
#define CACHEBUF_ADDR 0x00800000u  /* 8 MiB: wallpaper cache, same size as the back buffer */

static uint32_t *back;
static uint32_t *cache;
static int cache_valid;
static uint8_t  *front;

static uint32_t W, H, pitch, bytespp;

/* fast 32-bit fill */
static inline void fill32(uint32_t *d, uint32_t v, uint32_t n)
{
    __asm__ volatile ("cld; rep stosl"
                      : "+D"(d), "+c"(n)
                      : "a"(v)
                      : "memory");
}

/* fast 32-bit copy */
static inline void copy32(uint32_t *d, const uint32_t *s, uint32_t n)
{
    __asm__ volatile ("cld; rep movsl"
                      : "+D"(d), "+S"(s), "+c"(n)
                      :
                      : "memory");
}

/* a: 0..256, blends two channels per multiply */
static inline uint32_t blend(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t rb = ((s & 0xFF00FF) * a +
                   (d & 0xFF00FF) * (256 - a)) >> 8 & 0xFF00FF;

    uint32_t g = ((s & 0x00FF00) * a +
                  (d & 0x00FF00) * (256 - a)) >> 8 & 0x00FF00;

    return rb | g;
}

int gfx_init(void)
{
    if (!VBE_OK)
        return 0;

    W = VBE_W;
    H = VBE_H;
    pitch = VBE_PITCH;
    bytespp = VBE_BPP / 8;

    if (bytespp != 3 && bytespp != 4)
        return 0;

    front = (uint8_t *)VBE_FB;
    back = (uint32_t *)BACKBUF_ADDR;
    cache = (uint32_t *)CACHEBUF_ADDR;
    cache_valid = 0;
    return 1;
}

uint32_t gfx_w(void) { return W; }
uint32_t gfx_h(void) { return H; }

/* Wallpaper cache: render the wallpaper once, then restore it with one fast copy. */
void gfx_cache_save(void)
{
    copy32(cache, back, W * H);
    cache_valid = 1;
}

int gfx_cache_restore(void)
{
    if (!cache_valid)
        return 0;
    copy32(back, cache, W * H);
    return 1;
}

/* Trim a rectangle to the screen. Returns 0 if nothing is left. */
static int clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > (int)W) *w = (int)W - *x;
    if (*y + *h > (int)H) *h = (int)H - *y;
    return *w > 0 && *h > 0;
}

void gfx_rect(int x, int y, int w, int h, uint32_t c)
{
    if (!clip(&x, &y, &w, &h))
        return;

    for (int j = 0; j < h; j++) {
        uint32_t *p = back + (y + j) * W + x;
        uint32_t n = w;

        __asm__ volatile ("cld; rep stosl"
                          : "+D"(p), "+c"(n) : "a"(c) : "memory");
    }
}

void gfx_clear(uint32_t c)
{
    gfx_rect(0, 0, (int)W, (int)H, c);
}

void gfx_frame(int x, int y, int w, int h, uint32_t c)
{
    gfx_rect(x, y, w, 1, c);
    gfx_rect(x, y + h - 1, w, 1, c);
    gfx_rect(x, y, 1, h, c);
    gfx_rect(x + w - 1, y, 1, h, c);
}

static uint32_t lerp(uint32_t a, uint32_t b, int i, int n)
{
    uint32_t r = 0;

    for (int s = 0; s <= 16; s += 8) {
        int ca = (a >> s) & 255;
        int cb = (b >> s) & 255;
        r |= (uint32_t)(ca + (cb - ca) * i / n) << s;
    }
    return r;
}

void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    int n = h > 1 ? h - 1 : 1;

    for (int j = 0; j < h; j++)
        gfx_rect(x, y + j, w, 1, lerp(top, bottom, j, n));
}

/* alpha 0..255. Two channels per multiply and no per-pixel division. */
void gfx_blend_rect(int x, int y, int w, int h, uint32_t c, uint32_t alpha)
{
    uint32_t a = alpha > 255 ? 256 : alpha + (alpha >> 7);   /* 0..256 */
    uint32_t ia = 256 - a;
    uint32_t srb = (c & 0xFF00FF) * a;
    uint32_t sg  = (c & 0x00FF00) * a;

    if (!clip(&x, &y, &w, &h))
        return;

    for (int j = 0; j < h; j++) {
        uint32_t *p = back + (y + j) * W + x;

        for (int i = 0; i < w; i++, p++) {
            uint32_t d = *p;

            *p = (((srb + (d & 0xFF00FF) * ia) >> 8) & 0xFF00FF) |
                 (((sg  + (d & 0x00FF00) * ia) >> 8) & 0x00FF00);
        }
    }
}

void gfx_text(int x, int y, const char *s, uint32_t fg)
{
    for (; *s; s++, x += 8) {
        const uint8_t *g = FONT + (uint8_t)*s * 16;

        for (int j = 0; j < 16; j++) {
            int yy = y + j;

            if (yy < 0 || yy >= (int)H)
                continue;

            for (int i = 0; i < 8; i++) {
                int xx = x + i;

                if (xx < 0 || xx >= (int)W)
                    continue;

                if (g[j] & (0x80 >> i))
                    back[yy * W + xx] = fg;
            }
        }
    }
}

void gfx_flip_rect(int x, int y, int w, int h)
{
    if (!clip(&x, &y, &w, &h))
        return;

    for (int j = 0; j < h; j++) {
        uint32_t *src = back + (y + j) * W + x;
        uint8_t *dst = front + (y + j) * pitch + x * bytespp;

        if (bytespp == 4) {
            uint32_t n = w;

            __asm__ volatile ("cld; rep movsl"
                              : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
        } else {
            for (int i = 0; i < w; i++, dst += 3) {
                uint32_t c = src[i];

                dst[0] = c;
                dst[1] = c >> 8;
                dst[2] = c >> 16;
            }
        }
    }
}

void gfx_flip(void)
{
    gfx_flip_rect(0, 0, (int)W, (int)H);
}

void gfx_blit(const gfx_image *im, int x, int y)
{
    for (int j = 0; j < im->h; j++) {
        int yy = y + j;

        if (yy < 0 || yy >= (int)H)
            continue;

        for (int i = 0; i < im->w; i++) {
            int xx = x + i;
            uint32_t c = im->px[j * im->w + i];

            if (xx < 0 || xx >= (int)W || !(c >> 24))
                continue;

            back[yy * W + xx] = c & 0xFFFFFF;
        }
    }
}

void gfx_getrect(int x, int y, int w, int h, uint32_t *buf)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int xx = x + i, yy = y + j;

            buf[j * w + i] = (xx >= 0 && xx < (int)W && yy >= 0 && yy < (int)H)
                             ? back[yy * W + xx] : 0;
        }
}

void gfx_putrect(int x, int y, int w, int h, const uint32_t *buf)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int xx = x + i, yy = y + j;

            if (xx >= 0 && xx < (int)W && yy >= 0 && yy < (int)H)
                back[yy * W + xx] = buf[j * w + i];
        }
}

void gfx_capture(void)
{
    for (uint32_t j = 0; j < H; j++) {
        const uint8_t *s = front + j * pitch;
        uint32_t *d = back + j * W;

        if (bytespp == 4) {
            for (uint32_t i = 0; i < W; i++)
                d[i] = ((const uint32_t *)s)[i] & 0xFFFFFF;
        } else {
            for (uint32_t i = 0; i < W; i++)
                d[i] = s[i * 3] | (s[i * 3 + 1] << 8) | (s[i * 3 + 2] << 16);
        }
    }
}

void gfx_blit_scaled(const gfx_image *im, int x, int y, int w, int h)
{
    if (!im->px || w <= 0 || h <= 0)
        return;

    for (int j = 0; j < h; j++) {
        int yy = y + j;

        if (yy < 0 || yy >= (int)H)
            continue;

        for (int i = 0; i < w; i++) {
            int xx = x + i;
            uint32_t c = im->px[(j * im->h / h) * im->w + i * im->w / w];

            if (xx < 0 || xx >= (int)W || !(c >> 24))
                continue;

            back[yy * W + xx] = c & 0xFFFFFF;
        }
    }
}