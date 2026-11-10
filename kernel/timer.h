
#ifndef TIMER_H
#define TIMER_H

#include "ftfs.h"

void timer_init(u32 frequency);
u32 timer_ticks(void);
u32 timer_seconds(void);

#endif
