#ifndef TEXTFIELD_H
#define TEXTFIELD_H

/* textfield - tiny single-line text input widget for Yellowfin OS */

#define TF_MAX 24

/* editing keys: deskt.c / key_poll translate real scancodes to these.
   Printable ASCII (32..126) is typed as-is. */
#define TF_BACKSPACE 8
#define TF_DEL       0x1100
#define TF_LEFT      0x1101
#define TF_RIGHT     0x1102
#define TF_HOME      0x1103
#define TF_END       0x1104

typedef struct {
    char buf[TF_MAX + 1];
    int  len;
    int  cur;
} textfield;

void tf_clear(textfield *t);

/* returns 1 if the text or caret changed (redraw needed), else 0 */
int  tf_key(textfield *t, int key);

/* draws the box; shows placeholder (grey) while empty */
void tf_draw(const textfield *t, int x, int y, int w, int h,
             const char *placeholder, int caret_on);

#endif