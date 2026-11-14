#include "cmd.h"
#include "glass.h"
#include "fsh.h"

#define PROMPT "fish> "
#define PLEN   6
#define MAXIN  (CMD_COLS - PLEN - 1)

static char scr[CMD_ROWS][CMD_COLS + 1];
static int row, col;
static char in[MAXIN + 1];
static int inlen;

static void clear_row(int r) { for (int i = 0; i <= CMD_COLS; i++) scr[r][i] = 0; }

static void scroll(void)
{
    for (int r = 1; r < CMD_ROWS; r++)
        for (int i = 0; i <= CMD_COLS; i++)
            scr[r - 1][i] = scr[r][i];
    clear_row(CMD_ROWS - 1);
}

static void nl(void)
{
    col = 0;
    if (++row >= CMD_ROWS) { scroll(); row = CMD_ROWS - 1; }
    else clear_row(row);
}

static void put_c(char ch)
{
    if (ch == '\b') { if (col > 0) { col--; scr[row][col] = 0; } return; }
    if (ch == '\n') { nl(); return; }
    if (col >= CMD_COLS) nl();
    scr[row][col++] = ch;
}

static void put_s(const char *s) { while (*s) put_c(*s++); }

static void prompt(void) { put_s(PROMPT); inlen = 0; }

static int word_is(const char *w, int n, const char *cmd)
{
    int i = 0;
    for (; i < n; i++) {
        char c = w[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (!cmd[i] || cmd[i] != c) return 0;
    }
    return cmd[i] == 0;
}

/* fsh commands that take over the screen or block on keys; not allowed in a window */
static const char *const blocked[] = { "desktop", "install", "gfxtest", "mousetest", "spin" };

/* returns 1 if the window should close */
static int run_line(const char *l)
{
    int n = 0;

    while (*l == ' ') l++;
    if (!*l) return 0;
    while (l[n] && l[n] != ' ') n++;

    if (word_is(l, n, "exit"))
        return 1;

    if (word_is(l, n, "cls") || word_is(l, n, "clear")) {
        for (int r = 0; r < CMD_ROWS; r++) clear_row(r);
        row = col = 0;
        return 0;
    }

    for (unsigned i = 0; i < sizeof blocked / sizeof blocked[0]; i++) {
        if (word_is(l, n, blocked[i])) {
            put_s("Not available in a window. Use the shell (Q).\n");
            return 0;
        }
    }

    if (word_is(l, n, "dir"))
        fsh_exec("ls", put_c);
    else
        fsh_exec(l, put_c);
    return 0;
}

void cmd_reset(void)
{
    for (int r = 0; r < CMD_ROWS; r++) clear_row(r);
    row = col = 0;
    put_s("Yellowfin Command Prompt\n");
    prompt();
}

void cmd_draw(int x, int y)
{
    int ox = x + 8, oy = y + GLASS_TITLE_H + 8;

    gfx_rect(ox - 4, oy - 4, CMD_COLS * 8 + 8, CMD_ROWS * 16 + 8, 0x0A1424);
    for (int r = 0; r < CMD_ROWS; r++)
        if (scr[r][0])
            gfx_text(ox, oy + r * 16, scr[r], 0xD8E8F8);
    gfx_rect(ox + col * 8, oy + row * 16 + 13, 8, 2, 0xFFFFFF);   /* cursor */
}

int cmd_key(int k)
{
    if (k == '\n') {
        int quit;
        in[inlen] = 0;
        nl();
        quit = run_line(in);
        if (quit) return 2;
        if (col) nl();
        prompt();
        return 1;
    }
    if (k == 8 || k == 127) {
        if (inlen > 0) { inlen--; col--; scr[row][col] = 0; }
        return 1;
    }
    if (k >= 32 && k < 127) {
        if (inlen < MAXIN) { in[inlen++] = (char)k; put_c((char)k); }
        return 1;
    }
    return 0;
}