#include "graphics.h"

static int is_sp(char c) { return c == ' ' || c == '\t'; }

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const char *num(const char *s, int *v)
{
    int n = 0;

    while (is_sp(*s)) s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    *v = n;
    return s;
}

static const char *token(const char *s, const char **start, int *len)
{
    while (is_sp(*s)) s++;
    *start = s;
    while (*s && !is_sp(*s)) s++;
    *len = (int)(s - *start);
    return s;
}

static int ieq(const char *a, int n, const char *word)
{
    for (int i = 0; i < n; i++) {
        char c = a[i];

        if (c >= 'A' && c <= 'Z') c += 32;
        if (word[i] != c) return 0;
    }
    return word[n] == 0;
}

static uint32_t parse_color(const char *s, int n)
{
    if (ieq(s, n, "none"))  return 0;
    if (ieq(s, n, "white")) return 0xFFFFFFFF;
    if (ieq(s, n, "black")) return 0xFF000000;

    if (s[0] == '#' && (n == 7 || n == 13)) {
        int step = (n == 7) ? 2 : 4;   /* #RRGGBB or #RRRRGGGGBBBB */
        uint32_t c = 0;

        for (int k = 0; k < 3; k++) {
            int hi = hexv(s[1 + k * step]), lo = hexv(s[2 + k * step]);

            if (hi < 0 || lo < 0) return 0xFF000000;
            c = (c << 8) | (uint32_t)(hi * 16 + lo);
        }
        return 0xFF000000 | c;
    }
    return 0xFF000000;   /* unknown name -> black */
}

static struct { uint32_t key; uint32_t argb; } pal[256];

int xpm_load(gfx_image *out, const char *const *xpm, uint32_t *buf, int cap)
{
    int w, h, nc, cpp;
    const char *p = num(xpm[0], &w);

    p = num(p, &h);
    p = num(p, &nc);
    p = num(p, &cpp);

    if (w <= 0 || h <= 0 || w * h > cap || nc < 1 || nc > 256 || cpp < 1 || cpp > 4)
        return 0;

    for (int k = 0; k < nc; k++) {
        const char *line = xpm[1 + k];
        uint32_t key = 0;
        uint32_t argb = 0;
        int got = 0;

        for (int i = 0; i < cpp; i++) key = (key << 8) | (uint8_t)line[i];

        const char *s = line + cpp;
        const char *t, *v;
        int tl, vl;

        while (*s) {
            s = token(s, &t, &tl);
            if (!tl) break;
            s = token(s, &v, &vl);
            if (!vl) break;
            if (!got) { argb = parse_color(v, vl); got = 1; }   /* fallback: first value */
            if (tl == 1 && t[0] == 'c') { argb = parse_color(v, vl); break; }
        }
        pal[k].key = key;
        pal[k].argb = argb;
    }

    for (int y = 0; y < h; y++) {
        const char *row = xpm[1 + nc + y];

        for (int x = 0; x < w; x++) {
            uint32_t key = 0;
            uint32_t c = 0;

            for (int i = 0; i < cpp; i++) {
                char ch = row[x * cpp + i];

                if (!ch) return 0;
                key = (key << 8) | (uint8_t)ch;
            }
            for (int k = 0; k < nc; k++)
                if (pal[k].key == key) { c = pal[k].argb; break; }

            buf[y * w + x] = c;
        }
    }
    out->w = w;
    out->h = h;
    out->px = buf;
    return 1;
}