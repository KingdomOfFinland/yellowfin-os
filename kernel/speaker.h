#ifndef SPEAKER_H
#define SPEAKER_H

typedef struct { unsigned short freq; unsigned short ms; } note_t; // freq 0 = rest

void speaker_on(unsigned freq);
void speaker_off(void);
void speaker_beep(unsigned freq, unsigned ms);
// abort_fn: optional, return nonzero to stop (e.g. Esc pressed)
void speaker_play(const note_t *n, unsigned count, int (*abort_fn)(void));

#endif