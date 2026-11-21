/* explorer.c - Fish Explorer v2
   List mode: FTFS files, select (click / Up / Down), Open, Delete (click twice).
   View mode: read-only text viewer (ftfs_read), Up/Down scroll, Esc/Backspace/Back to return.

   Assumes gfx_text_mono(x, y, str, color) has the same signature as gfx_text. */

#include "explorer.h"
#include "glass.h"
#include "ftfs.h"

#define ROW_H    22
#define HEAD_H   22
#define FOOT_H   30
#define SB_W     16
#define PAD      12
#define TOOL_H   22
#define BTN_W    70
#define MAXF     64
#define NAME_MAX 20

#define VIEW_MAX   8192
#define VIEW_LINES 1024
#define MONO_W     8
#define MONO_H     16

enum { M_LIST, M_VIEW };

static int mode, sel, scroll, vis = 1, count, pending_del;
static char status[56];

/* viewer state */
static u8  vbuf[VIEW_MAX];
static u16 voff[VIEW_LINES];
static int vlen, vlines, vscroll, vvis = 1, vcols_built, vtrunc;
static char vname[NAME_MAX];

extern void gfx_text_mono(int x, int y, const char *s, uint32_t color);

/* ---- helpers ---- */

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static char *append(char *d, const char *s) { while (*s) *d++ = *s++; return d; }

static char *append_u(char *d, u32 n)
{
    char t[11];
    int i = 0;

    if (!n) *d++ = '0';
    while (n) { t[i++] = '0' + (n % 10); n /= 10; }
    while (i) *d++ = t[--i];
    return d;
}

static void set_status(const char *a, const char *b)
{
    char *e = status;
    int i = 0;

    e = append(e, a);
    if (b)
        while (b[i] && (e - status) < (int)sizeof(status) - 1)
            *e++ = b[i++];
    *e = 0;
}

static int count_files(void)
{
    char name[NAME_MAX];
    u32 size;
    int n = 0;

    while (n < MAXF && ftfs_entry(n, name, &size) == 0)
        n++;
    return n;
}

static int top_y(int y) { return y + GLASS_TITLE_H + 6; }

static void layout(int x, int y, int w, int h, int *lx, int *ly, int *lw)
{
    int lh;

    *lx = x + PAD;
    *ly = top_y(y) + TOOL_H + 6 + HEAD_H;
    *lw = w - 2 * PAD - SB_W - 4;
    lh = h - (*ly - y) - FOOT_H - 8;
    vis = lh / ROW_H;
    if (vis < 1) vis = 1;
}

static void clamp_scroll(void)
{
    int max = count - vis;

    if (max < 0) max = 0;
    if (scroll > max) scroll = max;
    if (scroll < 0) scroll = 0;
}

static void ensure_visible(void)
{
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + vis) scroll = sel - vis + 1;
    clamp_scroll();
}

/* ---- buttons ---- */

static void draw_button(int x, int y, const char *label, int enabled)
{
    gfx_rect(x, y, BTN_W, TOOL_H, enabled ? 0xBCD0E8 : 0xDCE6F2);
    gfx_frame(x, y, BTN_W, TOOL_H, 0x9FB6CE);
    gfx_text(x + (BTN_W - gfx_text_width(label)) / 2, y + 3, label,
             enabled ? 0x14283E : 0x8FA3B8);
}

static int btn_hit(int mx, int my, int x, int y)
{
    return inside(mx, my, x, y, BTN_W, TOOL_H);
}

/* ---- viewer ---- */

static void rebuild_lines(int cols)
{
    int pos = 0;

    vlines = 0;
    if (cols < 8) cols = 8;
    while (pos < vlen && vlines < VIEW_LINES) {
        int col = 0;

        voff[vlines++] = (u16)pos;
        while (pos < vlen && col < cols) {
            if (vbuf[pos] == '\n') break;
            pos++;
            col++;
        }
        if (pos < vlen && vbuf[pos] == '\n')
            pos++;
    }
    vcols_built = cols;
}

static void open_file(int idx)
{
    char name[NAME_MAX];
    u32 size;
    int n, i;

    if (ftfs_entry(idx, name, &size) != 0)
        return;

    n = ftfs_read(name, vbuf, VIEW_MAX);
    if (n < 0) {
        set_status("Could not read ", name);
        return;
    }

    for (i = 0; i < NAME_MAX; i++)
        vname[i] = name[i];
    for (i = 0; i < n; i++) {
        u8 c = vbuf[i];

        if (c == '\n') continue;
        if (c == '\t' || c == '\r') vbuf[i] = ' ';
        else if (c < 32 || c > 126) vbuf[i] = '.';
    }
    vlen = n;
    vtrunc = size > VIEW_MAX;
    vscroll = 0;
    vcols_built = 0;
    vlines = 0;
    mode = M_VIEW;
    status[0] = 0;
}

static void draw_view(int x, int y, int w, int h)
{
    int lx = x + PAD, ly = top_y(y) + TOOL_H + 6;
    int lw = w - 2 * PAD, lh = h - (ly - y) - FOOT_H - 8;
    int cols = (lw - 8) / MONO_W, i, maxs;
    char line[160], *e;

    if (cols > 150) cols = 150;
    if (cols != vcols_built)
        rebuild_lines(cols);

    vvis = (lh - 8) / MONO_H;
    if (vvis < 1) vvis = 1;
    maxs = vlines - vvis;
    if (maxs < 0) maxs = 0;
    if (vscroll > maxs) vscroll = maxs;
    if (vscroll < 0) vscroll = 0;

    draw_button(lx, top_y(y), "Back", 1);
    gfx_text(lx + BTN_W + 12, top_y(y) + 3, vname, 0x14283E);

    gfx_rect(lx, ly, lw, lh, 0xFFFFFF);
    gfx_frame(lx, ly, lw, lh, 0x9FB6CE);

    if (vlen == 0)
        gfx_text(lx + 8, ly + 6, "(empty file)", 0x2A5A8A);

    for (i = 0; i < vvis && vscroll + i < vlines; i++) {
        int s = voff[vscroll + i], n = 0;

        while (s + n < vlen && n < cols && vbuf[s + n] != '\n') {
            line[n] = (char)vbuf[s + n];
            n++;
        }
        line[n] = 0;
        gfx_text_mono(lx + 4, ly + 4 + i * MONO_H, line, 0x14283E);
    }

    e = append_u(line, (u32)(vlines ? vscroll + 1 : 0));
    e = append(e, "-");
    e = append_u(e, (u32)(vscroll + vvis < vlines ? vscroll + vvis : vlines));
    e = append(e, " of ");
    e = append_u(e, (u32)vlines);
    e = append(e, " lines");
    if (vtrunc)
        e = append(e, "  (file truncated to 8 KB)");
    *e = 0;
    gfx_text(lx, y + h - FOOT_H, line, 0x2A5A8A);
}

/* ---- list drawing ---- */

static void draw_file_icon(int x, int y)
{
    gfx_rect(x, y, 12, 15, 0xFFFFFF);
    gfx_frame(x, y, 12, 15, 0x5A7FA8);
    gfx_rect(x + 3, y + 4, 6, 1, 0x9FB6CE);
    gfx_rect(x + 3, y + 7, 6, 1, 0x9FB6CE);
    gfx_rect(x + 3, y + 10, 6, 1, 0x9FB6CE);
}

static void draw_arrow(int cx, int cy, int up)
{
    for (int r = 0; r < 4; r++) {
        int row = up ? cy - 2 + r : cy + 2 - r;

        gfx_rect(cx - r, row, 2 * r + 1, 1, 0x2A5A8A);
    }
}

static void fmt_size(char *buf, u32 size)
{
    char *e = buf;

    if (size >= 1024) {
        e = append_u(e, size / 1024);
        e = append(e, " KB");
    } else {
        e = append_u(e, size);
        e = append(e, " B");
    }
    *e = 0;
}

void explorer_reset(void)
{
    mode = M_LIST;
    sel = -1;
    scroll = 0;
    count = 0;
    pending_del = -1;
    status[0] = 0;
}

void explorer_draw(int x, int y, int w, int h)
{
    int lx, ly, lw, sbx, trk_h, i;
    char name[NAME_MAX], line[32], *e;
    u32 size;

    if (mode == M_VIEW) {
        draw_view(x, y, w, h);
        return;
    }

    layout(x, y, w, h, &lx, &ly, &lw);
    count = count_files();
    if (sel >= count) sel = count - 1;
    clamp_scroll();

    /* toolbar */
    draw_button(lx, top_y(y), "Open", sel >= 0);
    draw_button(lx + BTN_W + 8, top_y(y), "Delete", sel >= 0);

    /* column headers */
    gfx_text(lx + 28, ly - HEAD_H + 2, "Name", 0x2A5A8A);
    gfx_text(lx + lw - 80, ly - HEAD_H + 2, "Size", 0x2A5A8A);

    /* list */
    gfx_rect(lx, ly, lw, vis * ROW_H, 0xF4F8FC);
    gfx_frame(lx, ly, lw, vis * ROW_H, 0x9FB6CE);

    for (i = 0; i < vis; i++) {
        int idx = scroll + i, ry = ly + i * ROW_H;

        if (idx >= count || ftfs_entry(idx, name, &size) != 0)
            break;
        if (idx == sel)
            glass_highlight(lx + 1, ry, lw - 2, ROW_H);

        draw_file_icon(lx + 8, ry + 3);
        gfx_text(lx + 28, ry + 3, name, 0x14283E);
        fmt_size(line, size);
        gfx_text(lx + lw - 80, ry + 3, line, 0x14283E);
    }

    if (count == 0)
        gfx_text(lx + 12, ly + 6, "(no files)", 0x2A5A8A);

    /* scrollbar */
    sbx = lx + lw + 4;
    trk_h = vis * ROW_H;
    gfx_rect(sbx, ly, SB_W, trk_h, 0xDCE6F2);
    gfx_frame(sbx, ly, SB_W, trk_h, 0x9FB6CE);
    gfx_rect(sbx, ly, SB_W, SB_W, 0xBCD0E8);
    gfx_frame(sbx, ly, SB_W, SB_W, 0x9FB6CE);
    gfx_rect(sbx, ly + trk_h - SB_W, SB_W, SB_W, 0xBCD0E8);
    gfx_frame(sbx, ly + trk_h - SB_W, SB_W, SB_W, 0x9FB6CE);
    draw_arrow(sbx + SB_W / 2, ly + SB_W / 2, 1);
    draw_arrow(sbx + SB_W / 2, ly + trk_h - SB_W / 2, 0);

    if (count > vis) {
        int inner = trk_h - 2 * SB_W;
        int th = inner * vis / count, ty;

        if (th < 16) th = 16;
        ty = ly + SB_W + (inner - th) * scroll / (count - vis);
        gfx_rect(sbx + 1, ty, SB_W - 2, th, 0xA0C8F0);
        gfx_frame(sbx + 1, ty, SB_W - 2, th, 0xFFFFFF);
    }

    /* footer */
    {
        int fy = y + h - FOOT_H;

        if (status[0]) {
            gfx_text(lx, fy, status, 0x14283E);
        } else {
            e = append_u(line, (u32)count);
            e = append(e, count == 1 ? " item" : " items");
            *e = 0;
            gfx_text(lx, fy, line, 0x14283E);
        }

        e = append_u(line, ftfs_free_blocks());
        e = append(e, " blocks free");
        *e = 0;
        gfx_text(x + w - PAD - gfx_text_width(line), fy, line, 0x2A5A8A);
    }
}

/* ---- input ---- */

static void do_delete(void)
{
    char name[NAME_MAX];
    u32 size;

    if (sel < 0 || ftfs_entry(sel, name, &size) != 0)
        return;

    if (pending_del != sel) {                    /* first click: ask */
        pending_del = sel;
        set_status("Click Delete again to remove ", name);
        return;
    }

    pending_del = -1;
    if (ftfs_delete(name) == 0) {
        set_status("Deleted ", name);
        sel = -1;
    } else {
        set_status("Could not delete ", name);
    }
}

int explorer_click(int mx, int my, int x, int y, int w, int h)
{
    int lx, ly, lw, sbx, trk_h;

    if (mode == M_VIEW) {
        if (btn_hit(mx, my, x + PAD, top_y(y))) {
            mode = M_LIST;
            status[0] = 0;
            return 1;
        }
        return 0;
    }

    layout(x, y, w, h, &lx, &ly, &lw);
    sbx = lx + lw + 4;
    trk_h = vis * ROW_H;

    /* toolbar */
    if (btn_hit(mx, my, lx, top_y(y))) {
        status[0] = 0;
        pending_del = -1;
        if (sel >= 0) open_file(sel);
        return 1;
    }
    if (btn_hit(mx, my, lx + BTN_W + 8, top_y(y))) {
        status[0] = 0;
        do_delete();
        return 1;
    }

    status[0] = 0;
    pending_del = -1;

    /* rows: clicking the selected row again opens it */
    if (inside(mx, my, lx, ly, lw, trk_h)) {
        int idx = scroll + (my - ly) / ROW_H;

        if (idx < count) {
            if (idx == sel) open_file(idx);
            else            sel = idx;
        } else {
            sel = -1;
        }
        return 1;
    }

    /* scrollbar */
    if (inside(mx, my, sbx, ly, SB_W, trk_h)) {
        if (my < ly + SB_W)               scroll--;
        else if (my >= ly + trk_h - SB_W) scroll++;
        else if (count > vis) {
            int inner = trk_h - 2 * SB_W;
            int th = inner * vis / count, ty;

            if (th < 16) th = 16;
            ty = ly + SB_W + (inner - th) * scroll / (count - vis);
            if (my < ty)            scroll -= vis;
            else if (my >= ty + th) scroll += vis;
        }
        clamp_scroll();
        return 1;
    }

    return 1;       /* status was cleared, repaint */
}

int explorer_key(int k)
{
    if (mode == M_VIEW) {
        if (k == 0x80) { vscroll--; return 1; }
        if (k == 0x81) { vscroll++; return 1; }
        if (k == 27 || k == 8 || k == 127) {
            mode = M_LIST;
            status[0] = 0;
            return 1;
        }
        return 0;
    }

    if (k == 0x80) {
        if (sel > 0) sel--;
        else if (sel < 0 && count > 0) sel = count - 1;
        status[0] = 0;
        pending_del = -1;
        ensure_visible();
        return 1;
    }
    if (k == 0x81) {
        if (sel < count - 1) sel++;
        status[0] = 0;
        pending_del = -1;
        ensure_visible();
        return 1;
    }
    if (k == '\n') {
        if (sel >= 0) open_file(sel);
        return 1;
    }
    return 0;
}