#include "dirty.h"
#include "graphics.h"          /* for gfx_w() / gfx_h() */

#define MAX_DIRTY 16

typedef struct { int x0, y0, x1, y1; } rect_t;

static rect_t list[MAX_DIRTY];
static int count;

static int overlap(rect_t a, rect_t b)
{
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

static rect_t unite(rect_t a, rect_t b)
{
    rect_t r;
    r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    return r;
}

void dirty_add(int x, int y, int w, int h)
{
    rect_t r;
    int sw = (int)gfx_w(), sh = (int)gfx_h();

    r.x0 = x;     r.y0 = y;
    r.x1 = x + w; r.y1 = y + h;
    if (r.x0 < 0)  r.x0 = 0;
    if (r.y0 < 0)  r.y0 = 0;
    if (r.x1 > sw) r.x1 = sw;
    if (r.y1 > sh) r.y1 = sh;
    if (r.x0 >= r.x1 || r.y0 >= r.y1)
        return;

    for (int i = 0; i < count; i++) {
        if (overlap(list[i], r)) {
            list[i] = unite(list[i], r);
            return;
        }
    }

    if (count == MAX_DIRTY) {                 /* full: collapse into one box */
        for (int i = 1; i < count; i++)
            list[0] = unite(list[0], list[i]);
        count = 1;
        list[0] = unite(list[0], r);
    } else {
        list[count++] = r;
    }
}

int dirty_count(void) { return count; }

void dirty_get(int i, int *x, int *y, int *w, int *h)
{
    *x = list[i].x0;
    *y = list[i].y0;
    *w = list[i].x1 - list[i].x0;
    *h = list[i].y1 - list[i].y0;
}

void dirty_clear(void) { count = 0; }