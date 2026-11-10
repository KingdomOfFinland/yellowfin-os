#include "ui.h"
#include "timer.h"
#include "ata.h"

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

static int screen_welcome(uint8_t (*getkey)(void))
{
    backdrop("ENTER = Next    ESC = Cancel");
    window(WX, WY, WW, WH, " Yellowfin OS Setup ");

    ctext(WX, WW, WY + 3, "Welcome to Yellowfin OS", A_HEAD);
    ctext(WX, WW, WY + 5, "><(((('>", A_FISH);
    ctext(WX, WW, WY + 8,  "This wizard will install Yellowfin OS", A_WIN);
    ctext(WX, WW, WY + 9,  "on your computer.", A_WIN);
    ctext(WX, WW, WY + 12, "Version 0.0.1  -  Corn Kernel + Shark Loader", A_FISH);

    return key_wait(getkey);
}

static int screen_disk(uint8_t (*getkey)(void))
{
    int mb = (int)(ata_sector_count() / 2048);

    backdrop("ENTER = Next    ESC = Cancel");
    window(WX, WY, WW, WH, " Where do you want to install Yellowfin OS? ");

    text(WX + 3, WY + 3, "Available disks:", A_HEAD);

    fill(WX + 3, WY + 5, WW - 6, 1, ' ', A_SEL);
    text(WX + 4, WY + 5, "Disk 1  (primary slave)", A_SEL);
    number(WX + 40, WY + 5, mb, A_SEL);
    text(WX + 40 + 5, WY + 5, "MB", A_SEL);

    text(WX + 3, WY + 9, "Setup will use the whole disk.", A_WIN);

    return key_wait(getkey);
}

static int screen_confirm(uint8_t (*getkey)(void))
{
    backdrop("ENTER = Install    ESC = Cancel");
    window(WX, WY, WW, WH, " Ready to install ");

    ctext(WX, WW, WY + 4, "WARNING", A_WARN);
    ctext(WX, WW, WY + 6, "All data on Disk 1 will be erased.", A_WARN);
    ctext(WX, WW, WY + 9, "Press ENTER to begin installing.", A_WIN);

    return key_wait(getkey);
}

static const char *const steps[] = {
    "Creating GPT partition table",
    "Formatting FTFS filesystem",
    "Copying Shark Loader",
    "Copying Corn Kernel",
    "Writing boot configuration"
};
#define NSTEPS 5

static void screen_install(void)
{
    int i, p;

    backdrop("Installing...  please wait");
    window(WX, WY, WW, WH, " Installing Yellowfin OS ");

    for (i = 0; i < NSTEPS; i++)
        text(WX + 4, WY + 3 + i, steps[i], A_WIN);

    text(WX + 4, WY + 9, "Current step", A_HEAD);
    text(WX + 4, WY + 11, "Total", A_HEAD);

    for (i = 0; i < NSTEPS; i++) {
        text(WX + 2, WY + 3 + i, ">", A_FISH);
        for (p = 0; p <= 100; p += 5) {
            fill(WX + 4, WY + 10, 40, 1, ' ', A_WIN);
            bar(WX + 4, WY + 10, 40, p);
            fill(WX + 46, WY + 10, 5, 1, ' ', A_WIN);
            number(WX + 46, WY + 10, p, A_WIN);
            text(WX + 46 + (p >= 100 ? 3 : (p >= 10 ? 2 : 1)), WY + 10, "%", A_WIN);

            bar(WX + 4, WY + 12, 40, (i * 100 + p) / NSTEPS);
            wait_ticks(2);
        }
        text(WX + 2, WY + 3 + i, " ", A_WIN);
        text(WX + WW - 8, WY + 3 + i, "OK", A_GOOD);
    }
}

static void screen_done(uint8_t (*getkey)(void))
{
    backdrop("ENTER = Return to the shell");
    window(WX, WY, WW, WH, " Setup complete ");

    ctext(WX, WW, WY + 4, "><(((('>", A_FISH);
    ctext(WX, WW, WY + 6, "Yellowfin OS setup is complete.", A_HEAD);
    ctext(WX, WW, WY + 9, "(This was a preview: nothing was written to disk.)", A_WIN);

    key_wait(getkey);
}

void installer_run(uint8_t (*getkey)(void))
{
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

    if (screen_welcome(getkey) && screen_disk(getkey) && screen_confirm(getkey)) {
        screen_install();
        screen_done(getkey);
    }

    cursor_show();
}