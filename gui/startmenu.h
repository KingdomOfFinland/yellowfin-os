#ifndef STARTMENU_H
#define STARTMENU_H

#include "graphics.h"
#include "textfield.h"

/* actions returned by startmenu_act() / startmenu_key() */
enum { SM_EXPL, SM_CMD, SM_CALC, SM_INST, SM_ABOUT, SM_BIN, SM_FTFS, SM_EXIT,
       SM_FILE /* a file search result: name from startmenu_file_name() */ };

/* extra keys deskt.c translates for the menu (Enter is '\n', Esc is 27) */
#define SM_KEY_UP    0x1110
#define SM_KEY_DOWN  0x1111

/* startmenu_hit() returns this + row for search results while searching */
#define SM_SEARCH_BASE 100

/* hover = item index under the pointer or -1; logo = the fish logo image (used as the avatar) */
void startmenu_draw(int hover, gfx_image *logo);

/* -1 outside the panel, -2 inside but not on an item, else item index
   (SM_SEARCH_BASE + row for a search result) */
int  startmenu_hit(int mx, int my);

/* index from startmenu_hit -> SM_* action */
int  startmenu_act(int idx);
int  startmenu_press(int mx, int my);   /* 1 = took the press (a left-list item) */
int  startmenu_pressing(void);
int  startmenu_drag(int mx, int my);    /* 1 = needs redraw */
int  startmenu_release(void);           /* slot to launch if it was a plain click, else -1 */
void startmenu_rect(int *x, int *y, int *w, int *h);

/* ---- search ---- */

/* where file names come from (FTFS): count of slots and name of slot i
   (NULL or "" = unused slot). Call once at startup. */
void startmenu_set_file_source(int (*count)(void), const char *(*name)(int i));

/* clear the query; call whenever the menu opens or closes */
void startmenu_reset(void);

/* feed one key while the menu is open (printable ASCII, TF_* editing keys,
   '\n', 27 for Esc, SM_KEY_UP/DOWN).
   returns -1 = key not used (e.g. Esc with an empty box: close the menu yourself),
           -2 = changed, redraw the menu,
           >=0 = launch this SM_* action (SM_FILE: see startmenu_file_name()). */
int  startmenu_key(int key);

/* file name of the last picked SM_FILE result (from a click or Enter), or NULL */
const char *startmenu_file_name(void);

/* 1 while a query is typed (arrow keys should drive the result list, not the cursor) */
int  startmenu_searching(void);

/* caret blink: pass 0/1 from your timer; returns nothing, redraw yourself */
void startmenu_caret(int on);

#endif
