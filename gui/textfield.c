/* textfield.c - tiny single-line text input widget for Yellowfin OS */

#include "textfield.h"
#include "graphics.h"

void tf_clear(textfield *t)
{
    t->buf[0] = 0;
    t->len = 0;
    t->cur = 0;
}

int tf_key(textfield *t, int key)
{
    int i;

    if (key >= 32 && key < 127) {
        if (t->len >= TF_MAX)
            return 0;
        for (i = t->len; i >= t->cur; i--)
            t->buf[i + 1] = t->buf[i];
        t->buf[t->cur++] = (char)key;
        t->len++;
        return 1;
    }

    switch (key) {
    case TF_BACKSPACE:
        if (t->cur == 0)
            return 0;
        for (i = t->cur; i <= t->len; i++)
            t->buf[i - 1] = t->buf[i];
        t->cur--;
        t->len--;
        return 1;
    case TF_DEL:
        if (t->cur >= t->len)
            return 0;
        for (i = t->cur; i < t->len; i++)
            t->buf[i] = t->buf[i + 1];
        t->len--;
        return 1;
    case TF_LEFT:
        if (t->cur == 0)
            return 0;
        t->cur--;
        return 1;
    case TF_RIGHT:
        if (t->cur >= t->len)
            return 0;
        t->cur++;
        return 1;
    case TF_HOME:
        if (t->cur == 0)
            return 0;
        t->cur = 0;
        return 1;
    case TF_END:
        if (t->cur == t->len)
            return 0;
        t->cur = t->len;
        return 1;
    }
    return 0;
}

/* pixel width of the first n chars of s */
static int prefix_w(const char *s, int n)
{
    char tmp[TF_MAX + 1];
    int i;

    for (i = 0; i < n && s[i]; i++)
        tmp[i] = s[i];
    tmp[i] = 0;
    return gfx_text_width(tmp);
}

void tf_draw(const textfield *t, int x, int y, int w, int h,
             const char *placeholder, int caret_on)
{
    char vis[TF_MAX + 1];
    int start = 0, n, avail = w - 16, ty = y + (h - 16) / 2;

    gfx_rect(x, y, w, h, 0xFFFFFF);
    gfx_frame(x, y, w, h, 0x9FB6CE);

    if (t->len == 0) {
        gfx_text(x + 8, ty, placeholder, 0x8FA3B8);
    } else {
        /* scroll so the caret stays inside the box */
        while (start < t->cur && prefix_w(t->buf + start, t->cur - start) > avail)
            start++;

        /* copy as much as fits */
        for (n = 0; start + n < t->len; n++) {
            vis[n] = t->buf[start + n];
            vis[n + 1] = 0;
            if (gfx_text_width(vis) > avail) {
                vis[n] = 0;
                break;
            }
        }
        vis[n] = 0;
        gfx_text(x + 8, ty, vis, 0x14283E);
    }

    if (caret_on) {
        int cx = x + 8;

        if (t->len)
            cx += prefix_w(t->buf + start, t->cur - start);
        gfx_rect(cx, y + 5, 1, h - 10, 0x14283E);
    }
}