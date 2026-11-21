#ifndef DESKT_H
#define DESKT_H

#include <stdint.h>

void desktop_run(int (*poll_key)(void));
void busy_begin(void);
void busy_tick(void);
void busy_end(void);
void desktop_notify(void);

#endif
