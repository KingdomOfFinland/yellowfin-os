#ifndef TRAYFLY_H
#define TRAYFLY_H
enum { TF_NONE, TF_NOTIF, TF_NET, TF_VOL, TF_CLOCK };
void tf_open(int kind, int icon_cx);       // toggle; icon_cx = centre x of the clicked icon
int  tf_is_open(void);
// returns 1 if the click/move was consumed by the flyout (caller should redraw)
int  tf_pointer(int mx, int my, int btn, int prev_btn);
void trayfly_draw(void);                    // call after windows, before the cursor
int  tf_volume(void);
int  tf_muted(void);
#endif