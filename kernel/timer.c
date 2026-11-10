
#include "timer.h"
#include "idt.h"

static volatile u32 ticks = 0;

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void timer_handler(void)
{
    ticks++;
}

u32 timer_ticks(void)
{
    u32 value;

    __asm__ volatile ("cli");
    value = ticks;
    __asm__ volatile ("sti");

    return value;
}

u32 timer_seconds(void)
{
    return timer_ticks() / 100;
}

void timer_init(u32 frequency)
{
    u32 divisor;

    if (frequency == 0)
        return;

    divisor = 1193182 / frequency;

    if (divisor == 0 || divisor > 65535)
        return;

    /* Remap PIC IRQs to vectors 32-47. */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* Mask every IRQ except IRQ0 (the PIT). */
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);

    /* Configure PIT channel 0. */
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);

    idt_init();

    __asm__ volatile ("sti");
}
