#include "graphics.h"
#include "klog.h"

static inline uint8_t m_inb(uint16_t p)
{ uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void m_outb(uint16_t p, uint8_t v)
{ __asm__ volatile ("outb %0,%1" : : "a"(v), "Nd"(p)); }

static void wait_w(void) { for (int i = 0; i < 100000; i++) if (!(m_inb(0x64) & 2)) return; }
static int  wait_r(void) { for (int i = 0; i < 100000; i++) if (m_inb(0x64) & 1) return 1; return 0; }
static void cmd(uint8_t c)  { wait_w(); m_outb(0x64, c); }
static void data(uint8_t d) { wait_w(); m_outb(0x60, d); }

static uint8_t mouse_write(uint8_t b)
{
    cmd(0xD4);
    data(b);
    return wait_r() ? m_inb(0x60) : 0xEE;   /* 0xEE = no reply */
}

unsigned mouse_bytes;

static int mx, my, btn, idx;
static uint8_t pkt[3];

static void drain(void)
{
    for (int i = 0; i < 32 && (m_inb(0x64) & 1); i++) m_inb(0x60);
}

void mouse_init(void)
{
    uint8_t st, r_reset, r_defaults, r_stream;

    drain();

    cmd(0xA8);                       /* enable aux port */
    cmd(0x20);                       /* read config byte */
    wait_r();
    st = m_inb(0x60);
    st &= ~0x20;                     /* mouse clock on */
    st &= ~0x02;                     /* IRQ12 off, we poll */
    cmd(0x60);
    data(st);

    r_reset = mouse_write(0xFF);     /* reset: expect FA, then AA, 00 */
    if (r_reset == 0xFA) {
        if (wait_r()) m_inb(0x60);   /* self-test result (AA) */
        if (wait_r()) m_inb(0x60);   /* device id (00) */
    }
    drain();

    r_defaults = mouse_write(0xF6);
    r_stream   = mouse_write(0xF4);
    drain();
    idx = 0;

    kinfo("mouse", "init cfg=%02x reset=%02x defaults=%02x stream=%02x",
          st, r_reset, r_defaults, r_stream);

    mx = (int)gfx_w() / 2;
    my = (int)gfx_h() / 2;
}

void mouse_diag(void)
{
    uint8_t cfg = 0xEE, test = 0xEE, r, s0 = 0xEE, s1 = 0xEE, s2 = 0xEE;

    cmd(0x20);
    if (wait_r()) cfg = m_inb(0x60);

    cmd(0xA9);                            /* aux port self-test, 00 = pass */
    if (wait_r()) test = m_inb(0x60);

    r = mouse_write(0xE9);                /* status request */
    if (r == 0xFA) {
        if (wait_r()) s0 = m_inb(0x60);   /* bit 0x20 = reporting enabled */
        if (wait_r()) s1 = m_inb(0x60);   /* resolution */
        if (wait_r()) s2 = m_inb(0x60);   /* sample rate */
    }

    kinfo("mouse", "diag cfg=%02x auxtest=%02x e9=%02x st=%02x %02x %02x",
          cfg, test, r, s0, s1, s2);
}

int mouse_x(void) { return mx; }
int mouse_y(void) { return my; }
int mouse_buttons(void) { return btn; }   /* bit0 left, bit1 right, bit2 middle */

int mouse_poll(void)
{
    int changed = 0;

    while ((m_inb(0x64) & 0x21) == 0x21) {          /* output full + from mouse */
        uint8_t b = m_inb(0x60);

        mouse_bytes++;                              /* NEW */

        if (idx == 0 && !(b & 8)) continue;         /* out of sync, drop */
        pkt[idx++] = b;

        if (idx == 3) {
            idx = 0;
            if (pkt[0] & 0xC0) continue;            /* overflow */

            int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
            int dy = pkt[2] - ((pkt[0] << 3) & 0x100);

            mx += dx;
            my -= dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx >= (int)gfx_w()) mx = (int)gfx_w() - 1;
            if (my >= (int)gfx_h()) my = (int)gfx_h() - 1;
            btn = pkt[0] & 7;
            changed = 1;
        }
    }
    return changed;
}

/* ---- cursor drawing with save-under ---- */

static const gfx_image *cur;
static int hx, hy, sx, sy, shown;
static uint32_t under[32 * 32];

void mouse_cursor_hide(void)
{
    if (!shown) return;
    gfx_putrect(sx, sy, cur->w, cur->h, under);
    gfx_flip_rect(sx, sy, cur->w, cur->h);
    shown = 0;
}

void mouse_cursor_show(void)
{
    if (!cur || shown) return;
    sx = mx - hx;
    sy = my - hy;
    gfx_getrect(sx, sy, cur->w, cur->h, under);
    gfx_blit(cur, sx, sy);
    gfx_flip_rect(sx, sy, cur->w, cur->h);
    shown = 1;
}

void mouse_cursor_update(void) { mouse_cursor_hide(); mouse_cursor_show(); }

void mouse_set_cursor(const gfx_image *im, int hotx, int hoty)
{
    int was = shown;

    if (im->w > 32 || im->h > 32) return;
    mouse_cursor_hide();
    cur = im; hx = hotx; hy = hoty;
    if (was) mouse_cursor_show();
}

void mouse_move_by(int dx, int dy)
{
    mx += dx; my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= (int)gfx_w()) mx = (int)gfx_w() - 1;
    if (my >= (int)gfx_h()) my = (int)gfx_h() - 1;
}

void mouse_probe(void)
{
    uint8_t r, p0 = 0xEE, p1 = 0xEE, p2 = 0xEE;

    mouse_write(0xF0);                    /* remote mode: only sends on request */
    r = mouse_write(0xEB);                /* read data */
    if (r == 0xFA) {
        if (wait_r()) p0 = m_inb(0x60);
        if (wait_r()) p1 = m_inb(0x60);
        if (wait_r()) p2 = m_inb(0x60);
    }
    kinfo("mouse", "probe r=%02x pkt=%02x %02x %02x", r, p0, p1, p2);
}