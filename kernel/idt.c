#include "idt.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 flags;
    u16 offset_high;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void irq0_stub(void);
extern unsigned int exc_table[32];

void idt_set_gate(int n, unsigned int handler)
{
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void)
{
    int i;

    /* 1. clear the whole table */
    for (i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].flags = 0;
        idt[i].offset_high = 0;
    }

    /* 2. CPU exceptions 0-31 */
    for (i = 0; i < 32; i++)
        idt_set_gate(i, exc_table[i]);

    /* 3. timer IRQ */
    idt_set_gate(32, (unsigned int)irq0_stub);

    /* 4. load it */
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (u32)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}