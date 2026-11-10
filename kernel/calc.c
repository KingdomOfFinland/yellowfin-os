/*
 * Yellowfin OS calculator client.
 * This file contains only calculator state and drawing/input code.
 * Window creation, focus, dragging and desktop input polling stay in deskt.c.
 */
#include "calc.h"
#include "glass.h"

#define CALC_XPAD       12
#define CALC_DISPLAY_Y  (GLASS_TITLE_H + 12)
#define CALC_DISPLAY_H  44
#define CALC_BUTTON_Y   (GLASS_TITLE_H + 66)
#define CALC_BUTTON_W   52
#define CALC_BUTTON_H   34
#define CALC_GAP        5
#define CALC_ROW_GAP    4
#define CALC_LIMIT      999999999999LL

static long long calc_value;
static long long calc_accumulator;
static char calc_pending;
static int calc_new_entry;
static int calc_error;
static char calc_display[24];

static void calc_format(long long value, char *out)
{
    char temp[24];
    int n = 0;
    unsigned long long magnitude;

    if (value < 0)
        magnitude = (unsigned long long)(-(value + 1)) + 1ULL;
    else
        magnitude = (unsigned long long)value;

    do {
        temp[n++] = (char)('0' + (magnitude % 10ULL));
        magnitude /= 10ULL;
    } while (magnitude && n < 22);

    if (value < 0)
        *out++ = '-';

    while (n)
        *out++ = temp[--n];

    *out = 0;
}

static void calc_update_display(void)
{
    if (calc_error) {
        calc_display[0] = 'E';
        calc_display[1] = 'R';
        calc_display[2] = 'R';
        calc_display[3] = 0;
    } else {
        calc_format(calc_value, calc_display);
    }
}

static void calc_clear(void)
{
    calc_value = 0;
    calc_accumulator = 0;
    calc_pending = 0;
    calc_new_entry = 0;
    calc_error = 0;
    calc_update_display();
}

void calc_reset(void)
{
    calc_clear();
}

static long long calc_clamp(long long value)
{
    if (value > CALC_LIMIT)
        return CALC_LIMIT;
    if (value < -CALC_LIMIT)
        return -CALC_LIMIT;
    return value;
}

static void calc_digit(int digit)
{
    if (calc_error)
        calc_clear();

    if (calc_new_entry) {
        calc_value = 0;
        calc_new_entry = 0;
    }

    if (calc_value > 0) {
        if (calc_value > (CALC_LIMIT - digit) / 10)
            return;
        calc_value = calc_value * 10 + digit;
    } else if (calc_value < 0) {
        if (calc_value < (-CALC_LIMIT + digit) / 10)
            return;
        calc_value = calc_value * 10 - digit;
    } else {
        calc_value = digit;
    }

    calc_update_display();
}

static void calc_backspace(void)
{
    if (calc_error) {
        calc_clear();
        return;
    }

    if (calc_new_entry) {
        calc_value = 0;
        calc_new_entry = 0;
    } else if (calc_value < 0) {
        calc_value = -((-calc_value) / 10);
    } else {
        calc_value /= 10;
    }

    calc_update_display();
}

static void calc_toggle_sign(void)
{
    if (calc_error)
        return;
    calc_value = -calc_value;
    calc_new_entry = 0;
    calc_update_display();
}

static int calc_apply(char op, long long a, long long b, long long *result)
{
    switch (op) {
    case '+':
        *result = calc_clamp(a + b);
        return 1;

    case '-':
        *result = calc_clamp(a - b);
        return 1;

    case '*':
        if (a && b) {
            unsigned long long aa = (a < 0) ? (unsigned long long)(-a) : (unsigned long long)a;
            unsigned long long bb = (b < 0) ? (unsigned long long)(-b) : (unsigned long long)b;
            if (aa > (unsigned long long)CALC_LIMIT / bb) {
                *result = ((a < 0) != (b < 0)) ? -CALC_LIMIT : CALC_LIMIT;
                return 1;
            }
        }
        *result = a * b;
        return 1;

    case '/':
        if (!b)
            return 0;
        *result = a / b;
        return 1;

    case '%':
        if (!b)
            return 0;
        *result = a % b;
        return 1;
    }

    return 0;
}

static void calc_set_operator(char op)
{
    long long result;

    if (calc_error)
        return;

    if (calc_pending && !calc_new_entry) {
        if (!calc_apply(calc_pending, calc_accumulator, calc_value, &result)) {
            calc_error = 1;
            calc_update_display();
            return;
        }
        calc_value = result;
        calc_accumulator = result;
    } else {
        calc_accumulator = calc_value;
    }

    calc_pending = op;
    calc_new_entry = 1;
    calc_update_display();
}

static void calc_equals(void)
{
    long long result;

    if (calc_error || !calc_pending)
        return;

    if (!calc_apply(calc_pending, calc_accumulator, calc_value, &result)) {
        calc_error = 1;
        calc_pending = 0;
        calc_update_display();
        return;
    }

    calc_value = result;
    calc_accumulator = result;
    calc_pending = 0;
    calc_new_entry = 1;
    calc_update_display();
}

static void calc_press(char key)
{
    if (key >= '0' && key <= '9') {
        calc_digit(key - '0');
        return;
    }

    switch (key) {
    case 'C':
    case 'c':
        calc_clear();
        break;
    case 'B':
        calc_backspace();
        break;
    case 'S':
        calc_toggle_sign();
        break;
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
        calc_set_operator(key);
        break;
    case '=':
    case '\n':
    case '\r':
        calc_equals();
        break;
    }
}

int calc_key(int k)
{
    if (k >= '0' && k <= '9') {
        calc_press((char)k);
        return 1;
    }

    if (k == '+' || k == '-' || k == '*' || k == '/' || k == '%') {
        calc_press((char)k);
        return 1;
    }

    if (k == '=' || k == '\n' || k == '\r') {
        calc_press('=');
        return 1;
    }

    if (k == 'c' || k == 'C') {
        calc_press('C');
        return 1;
    }

    if (k == 8 || k == 127) {
        calc_press('B');
        return 1;
    }

    if (k == '~') {
        calc_press('S');
        return 1;
    }

    return 0;
}

static void calc_draw_button(int x, int y, int w, const char *label, int special)
{
    uint32_t fill = special ? 0xB8D8F4 : 0xDCEAF8;
    int tx = x + (w - 8) / 2;

    gfx_blend_rect(x, y, w, CALC_BUTTON_H, fill, 205);
    gfx_frame(x, y, w, CALC_BUTTON_H, 0xFFFFFF);
    gfx_text(tx, y + 9, label, 0x10243A);
}

void calc_draw(int x, int y)
{
    static const char *labels[4][4] = {
        { "C", "<-", "/", "*" },
        { "7", "8", "9", "-" },
        { "4", "5", "6", "+" },
        { "1", "2", "3", "=" }
    };
    int bx = x + CALC_XPAD;
    int by = y + CALC_BUTTON_Y;

    gfx_blend_rect(x + CALC_XPAD, y + CALC_DISPLAY_Y,
                   CALC_W - CALC_XPAD * 2, CALC_DISPLAY_H, 0xF4F8FC, 230);
    gfx_frame(x + CALC_XPAD, y + CALC_DISPLAY_Y,
              CALC_W - CALC_XPAD * 2, CALC_DISPLAY_H, 0xFFFFFF);
    gfx_text(x + CALC_XPAD + 10, y + CALC_DISPLAY_Y + 14,
             calc_display, 0x10243A);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int xx = bx + col * (CALC_BUTTON_W + CALC_GAP);
            int yy = by + row * (CALC_BUTTON_H + CALC_ROW_GAP);
            calc_draw_button(xx, yy, CALC_BUTTON_W, labels[row][col],
                             row == 0 || col == 3);
        }
    }

    /* Bottom row: zero spans two columns, then sign and remainder. */
    {
        int yy = by + 4 * (CALC_BUTTON_H + CALC_ROW_GAP);
        calc_draw_button(bx, yy, CALC_BUTTON_W * 2 + CALC_GAP, "0", 0);
        calc_draw_button(bx + 2 * (CALC_BUTTON_W + CALC_GAP), yy,
                         CALC_BUTTON_W, "+/-", 0);
        calc_draw_button(bx + 3 * (CALC_BUTTON_W + CALC_GAP), yy,
                         CALC_BUTTON_W, "%", 1);
    }
}

int calc_click(int mx, int my, int x, int y)
{
    int bx = x + CALC_XPAD;
    int by = y + CALC_BUTTON_Y;
    int relx = mx - bx;
    int rely = my - by;
    int row, col;

    if (relx < 0 || rely < 0)
        return 0;

    row = rely / (CALC_BUTTON_H + CALC_ROW_GAP);
    col = relx / (CALC_BUTTON_W + CALC_GAP);

    if (row < 0 || row >= 5 || col < 0 || col >= 4)
        return 0;

    if ((rely % (CALC_BUTTON_H + CALC_ROW_GAP)) >= CALC_BUTTON_H)
        return 0;

    if ((relx % (CALC_BUTTON_W + CALC_GAP)) >= CALC_BUTTON_W &&
        !(row == 4 && col == 1))
        return 0;

    if (row == 4) {
        if (col <= 1)
            calc_press('0');
        else if (col == 2)
            calc_press('S');
        else
            calc_press('%');
        return 1;
    }

    if (row == 0) {
        static const char keys[] = { 'C', 'B', '/', '*' };
        calc_press(keys[col]);
    } else if (row == 1) {
        static const char keys[] = { '7', '8', '9', '-' };
        calc_press(keys[col]);
    } else if (row == 2) {
        static const char keys[] = { '4', '5', '6', '+' };
        calc_press(keys[col]);
    } else {
        static const char keys[] = { '1', '2', '3', '=' };
        calc_press(keys[col]);
    }

    return 1;
}