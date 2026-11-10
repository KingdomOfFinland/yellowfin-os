#include "speaker.h"

#define PIT_HZ 1193182u

// --- adapt these to your timer.c ---
extern volatile unsigned timer_ticks;
#define TIMER_HZ 1000u
// -----------------------------------

static inline void outb(unsigned short p, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline unsigned char inb(unsigned short p) {
    unsigned char v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

void speaker_on(unsigned freq) {
    if (freq < 20) { speaker_off(); return; }
    unsigned d = PIT_HZ / freq;
    if (d > 0xFFFF) d = 0xFFFF;
    outb(0x43, 0xB6);              // ch2, lobyte/hibyte, square wave
    outb(0x42, d & 0xFF);
    outb(0x42, (d >> 8) & 0xFF);
    unsigned char t = inb(0x61);
    if ((t & 3) != 3) outb(0x61, t | 3);
}

void speaker_off(void) {
    outb(0x61, inb(0x61) & 0xFC);
}

static unsigned ms_to_ticks(unsigned ms) {
    return (ms * TIMER_HZ + 999) / 1000;
}

static void wait_until(unsigned target) {
    while ((int)(timer_ticks - target) < 0)
        __asm__ volatile ("hlt");
}

void speaker_beep(unsigned freq, unsigned ms) {
    speaker_on(freq);
    wait_until(timer_ticks + ms_to_ticks(ms));
    speaker_off();
}

void speaker_play(const note_t *n, unsigned count, int (*abort_fn)(void)) {
    unsigned t = timer_ticks;      // absolute schedule, so no drift
    for (unsigned i = 0; i < count; i++) {
        if (abort_fn && abort_fn()) break;
        if (n[i].freq) speaker_on(n[i].freq); else speaker_off();
        t += ms_to_ticks(n[i].ms);
        wait_until(t);
    }
    speaker_off();
}