#ifndef DIRTY_H
#define DIRTY_H

void dirty_add(int x, int y, int w, int h);
int  dirty_count(void);
void dirty_get(int i, int *x, int *y, int *w, int *h);
void dirty_clear(void);

#endif