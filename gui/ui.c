#include "ui.h"
#include "timer.h"
#include "install.h"
#include "payload.h"

#define W 80
#define H 25

int fbcon_on(void);
void fbcon_draw_at(char ch, uint32_t col, uint32_t row, uint8_t attr);
void fbcon_clear(uint8_t attr);
uint32_t fbcon_cols(void);
uint32_t fbcon_rows(void);

static uint16_t shadow[W * H];   /* copy of what the setup screen shows */
static int ox, oy;               /* offset that centres 80x25 on a bigger screen */

/* CP437 double-line box characters */
#define D_H  0xCD
#define D_V  0xBA
#define D_BL 0xC8
#define D_BR 0xBC

/* attributes: (background << 4) | foreground. Backgrounds stay below 8
   so they never blink. */
#define A_DESK   0x19   /* light blue shade on blue */
#define A_HEADER 0x1E   /* yellow on blue           */
#define A_FOOTER 0x30   /* black on cyan            */
#define A_TITLE  0x3F   /* white on cyan            */
#define A_CLOSE  0x4F   /* white on red             */
#define A_WIN    0x70   /* black on gray            */
#define A_HEAD   0x71   /* blue on gray             */
#define A_FISH   0x76   /* brown on gray            */
#define A_WARN   0x74   /* red on gray              */
#define A_GOOD   0x72   /* green on gray            */
#define A_SEL    0x1F   /* white on blue            */

#define KEY_UP   0x80
#define KEY_DOWN 0x81

static volatile uint16_t *const VGA = (volatile uint16_t *)0xB8000;

static inline void outb(uint16_t p, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}

static void cursor_hide(void) { outb(0x3D4, 0x0A); outb(0x3D5, 0x20); }
static void cursor_show(void)
{
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
}

/* ---------- drawing helpers ---------- */
static void cell(int x, int y, uint8_t ch, uint8_t attr)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;

    shadow[y * W + x] = ((uint16_t)attr << 8) | ch;

    if (fbcon_on())
        fbcon_draw_at((char)ch, x + ox, y + oy, attr);
    else
        VGA[y * W + x] = ((uint16_t)attr << 8) | ch;
}

static void shade(int x, int y)       /* darken a cell, keep its character */
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    cell(x, y, (uint8_t)(shadow[y * W + x] & 0x00FF), 0x08);
}

static void fill(int x, int y, int w, int h, uint8_t ch, uint8_t attr)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            cell(x + i, y + j, ch, attr);
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void text(int x, int y, const char *s, uint8_t attr)
{
    while (*s) cell(x++, y, (uint8_t)*s++, attr);
}

static void ctext(int x, int w, int y, const char *s, uint8_t attr)
{
    text(x + (w - slen(s)) / 2, y, s, attr);
}

static void number(int x, int y, int v, uint8_t attr)
{
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) cell(x++, y, (uint8_t)tmp[--n], attr);
}

/* "1234 MB" into out */
static void fmt_mb(char *out, uint32_t mb)
{
    char t[11];
    int n = 0, i = 0;

    if (!mb) t[n++] = '0';
    while (mb) { t[n++] = (char)('0' + mb % 10); mb /= 10; }
    while (n) out[i++] = t[--n];
    out[i++] = ' '; out[i++] = 'M'; out[i++] = 'B'; out[i] = 0;
}

/* "Primary master - 1234 MB" into out (at least 48 bytes) */
static void disk_line(char *out, const struct inst_disk *d)
{
    int n = 0;
    const char *s = d->name;

    while (*s) out[n++] = *s++;
    out[n++] = ' '; out[n++] = '-'; out[n++] = ' ';
    fmt_mb(out + n, d->sectors / 2048);
}

static void bar(int x, int y, int w, int pct)
{
    int i, filled = w * pct / 100;
    for (i = 0; i < w; i++)
        cell(x + i, y, i < filled ? 0xDB : 0xB0, i < filled ? 0x72 : 0x78);
}

static void window(int x, int y, int w, int h, const char *title)
{
    int i;

    for (i = 0; i < h; i++) { shade(x + w, y + 1 + i); shade(x + w + 1, y + 1 + i); }
    for (i = 0; i < w; i++) shade(x + 2 + i, y + h);

    fill(x, y, w, h, ' ', A_WIN);
    for (i = 1; i < w - 1; i++) cell(x + i, y + h - 1, D_H, A_HEAD);
    for (i = 1; i < h - 1; i++) {
        cell(x, y + i, D_V, A_HEAD);
        cell(x + w - 1, y + i, D_V, A_HEAD);
    }
    cell(x, y + h - 1, D_BL, A_HEAD);
    cell(x + w - 1, y + h - 1, D_BR, A_HEAD);

    fill(x, y, w, 1, ' ', A_TITLE);
    ctext(x, w, y, title, A_TITLE);
    text(x + w - 4, y, " X ", A_CLOSE);
}

static void backdrop(const char *footer)
{
    fill(0, 0, W, H, 0xB0, A_DESK);
    fill(0, 0, W, 1, ' ', A_HEADER);
    text(1, 0, "Yellowfin OS Setup", A_HEADER);
    fill(0, H - 1, W, 1, ' ', A_FOOTER);
    text(1, H - 1, footer, A_FOOTER);
}

static void wait_ticks(uint32_t n)
{
    uint32_t start = timer_ticks();
    while (timer_ticks() - start < n)
        __asm__ volatile ("hlt");
}

/* 1 = Enter, 0 = Esc */
static int key_wait(uint8_t (*getkey)(void))
{
    for (;;) {
        uint8_t k = getkey();
        if (k == '\n') return 1;
        if (k == 27)   return 0;
    }
}

/* ---------- screens ---------- */
#define WX 10
#define WY 4
#define WW 60
#define WH 16

static void screen_unavailable(uint8_t (*getkey)(void))
{
    backdrop("ESC = Back");
    window(WX, WY, WW, WH, " Setup is not available ");

    ctext(WX, WW, WY + 4, "Setup needs the Yellowfin boot image.", A_WARN);
    ctext(WX, WW, WY + 7, "Boot from the ISO or yellowfin.img,", A_WIN);
    ctext(WX, WW, WY + 8, "not with QEMU's -kernel option.", A_WIN);

    while (getkey() != 27) { }
}

static int screen_welcome(uint8_t (*getkey)(void))
{
    backdrop("ENTER = Next    ESC = Cancel");
    window(WX, WY, WW, WH, " Yellowfin OS Setup ");

    ctext(WX, WW, WY + 3, "Welcome to Yellowfin OS", A_HEAD);
    ctext(WX, WW, WY + 5, "><(((('>", A_FISH);
    ctext(WX, WW, WY + 8,  "This wizard will install Yellowfin OS", A_WIN);
    ctext(WX, WW, WY + 9,  "on your computer.", A_WIN);
    ctext(WX, WW, WY + 12, "Version 0.2  -  Corn Kernel + Shark Loader", A_FISH);

    return key_wait(getkey);
}

/* 1 = a disk was chosen (index in *chosen), 0 = cancelled */
static int screen_disk(uint8_t (*getkey)(void), const struct inst_disk *d, int n, int *chosen)
{
    int sel = 0, i;

    for (i = 0; i < n; i++)
        if (d[i].usable) { sel = i; break; }

    for (;;) {
        uint8_t k;

        backdrop("UP/DOWN = Choose    ENTER = Next    ESC = Cancel");
        window(WX, WY, WW, WH, " Where do you want to install Yellowfin OS? ");

        text(WX + 3, WY + 3, "Available disks:", A_HEAD);

        if (n == 0)
            text(WX + 4, WY + 5, "No ATA disks found.", A_WARN);

        for (i = 0; i < n; i++) {
            uint8_t a = (i == sel) ? A_SEL : A_WIN;
            int y = WY + 5 + i;
            char sz[16];

            fmt_mb(sz, d[i].sectors / 2048);
            fill(WX + 3, y, WW - 6, 1, ' ', a);
            text(WX + 4, y, d[i].name, a);
            text(WX + 24, y, sz, a);
            if (d[i].note)
                text(WX + 38, y, d[i].note, a);
        }

        text(WX + 3, WY + 11, "Setup will erase the chosen disk.", A_WIN);

        k = getkey();
        if (k == 27) return 0;
        if (k == KEY_UP && sel > 0) sel--;
        else if (k == KEY_DOWN && sel < n - 1) sel++;
        else if (k == '\n' && n > 0 && d[sel].usable) { *chosen = sel; return 1; }
    }
}

static int screen_confirm(uint8_t (*getkey)(void), const struct inst_disk *d)
{
    char line[48];

    disk_line(line, d);

    backdrop("ENTER = Install    ESC = Cancel");
    window(WX, WY, WW, WH, " Ready to install ");

    ctext(WX, WW, WY + 3, "WARNING", A_WARN);
    ctext(WX, WW, WY + 5, "All data on this disk will be erased:", A_WARN);
    ctext(WX, WW, WY + 7, line, A_HEAD);
    ctext(WX, WW, WY + 10, "Press ENTER to begin installing.", A_WIN);

    return key_wait(getkey);
}

#define NSTEPS 5
static const char *const steps[NSTEPS] = {
    "Checking disk",
    "Copying Corn Kernel",
    "Formatting FTFS filesystem",
    "Copying Shark Loader",
    "Verifying installation"
};

static int cur_step;

/* called by install_run() as it works */
static void ui_progress(int step, int pct)
{
    char pc[8];
    int n = 0, v = pct;

    if (step != cur_step) {
        if (cur_step >= 0) {
            text(WX + 2, WY + 3 + cur_step, " ", A_WIN);
            text(WX + WW - 8, WY + 3 + cur_step, "OK", A_GOOD);
        }
        cur_step = step;
        text(WX + 2, WY + 3 + step, ">", A_FISH);
    }

    fill(WX + 4, WY + 10, 40, 1, ' ', A_WIN);
    bar(WX + 4, WY + 10, 40, pct);

    if (v == 0) pc[n++] = '0';
    {
        char t[4];
        int m = 0;
        while (v > 0) { t[m++] = (char)('0' + v % 10); v /= 10; }
        while (m) pc[n++] = t[--m];
    }
    pc[n++] = '%';
    pc[n] = 0;
    fill(WX + 46, WY + 10, 6, 1, ' ', A_WIN);
    text(WX + 46, WY + 10, pc, A_WIN);

    bar(WX + 4, WY + 12, 40, (step * 100 + pct) / NSTEPS);
}

static int screen_install(const struct inst_disk *d)
{
    int i, err;

    backdrop("Installing...  do not turn off the computer");
    window(WX, WY, WW, WH, " Installing Yellowfin OS ");

    for (i = 0; i < NSTEPS; i++)
        text(WX + 4, WY + 3 + i, steps[i], A_WIN);

    text(WX + 4, WY + 9, "Current step", A_HEAD);
    text(WX + 4, WY + 11, "Total", A_HEAD);

    cur_step = -1;
    err = install_run(d, ui_progress);

    if (err == INST_OK) {
        text(WX + 2, WY + 3 + cur_step, " ", A_WIN);
        text(WX + WW - 8, WY + 3 + cur_step, "OK", A_GOOD);
        bar(WX + 4, WY + 12, 40, 100);
        wait_ticks(50);
    }
    return err;
}

static void screen_done(uint8_t (*getkey)(void), const struct inst_disk *d)
{
    char line[48];

    disk_line(line, d);

    backdrop("ENTER = Return to the desktop");
    window(WX, WY, WW, WH, " Setup complete ");

    ctext(WX, WW, WY + 3, "><(((('>", A_FISH);
    ctext(WX, WW, WY + 5, "Yellowfin OS was installed on:", A_HEAD);
    ctext(WX, WW, WY + 6, line, A_HEAD);
    ctext(WX, WW, WY + 9,  "Restart and boot from that disk", A_WIN);
    ctext(WX, WW, WY + 10, "to start your new Yellowfin OS.", A_WIN);

    key_wait(getkey);
}

static void screen_error(uint8_t (*getkey)(void), int err)
{
    backdrop("ENTER = Return to the desktop");
    window(WX, WY, WW, WH, " Setup failed ");

    ctext(WX, WW, WY + 4, "Installation did not finish.", A_WARN);
    ctext(WX, WW, WY + 7, install_error(err), A_WIN);
    ctext(WX, WW, WY + 10, "The disk was not made bootable.", A_WIN);

    key_wait(getkey);
}

void installer_run(uint8_t (*getkey)(void))
{
    struct inst_disk disks[INST_MAX_DISKS];
    int n, chosen = 0, err;

    cursor_hide();

    if (fbcon_on()) {
        ox = ((int)fbcon_cols() - W) / 2;
        oy = ((int)fbcon_rows() - H) / 2;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        fbcon_clear(0x07);            /* black margins around the setup window */
    } else {
        ox = 0;
        oy = 0;
    }

    if (!payload_ok()) {
        screen_unavailable(getkey);
    } else if (screen_welcome(getkey)) {
        n = install_probe(disks, INST_MAX_DISKS);

        if (screen_disk(getkey, disks, n, &chosen) &&
            screen_confirm(getkey, &disks[chosen])) {
            err = screen_install(&disks[chosen]);
            if (err == INST_OK) screen_done(getkey, &disks[chosen]);
            else                screen_error(getkey, err);
        }
    }

    cursor_show();
}
