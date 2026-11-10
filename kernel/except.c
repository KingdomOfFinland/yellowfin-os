#include <stdint.h>
#include "klog.h"
#include "panic.h"

struct regs {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;   /* pusha */
    uint32_t vector, err;
    uint32_t eip, cs, eflags;
};

static const char *const names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor overrun", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection fault", "Page fault", "Reserved",
    "x87 FPU error", "Alignment check", "Machine check", "SIMD FP exception",
    "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

void exception_handler(struct regs *r)
{
    const char *name = r->vector < 32 ? names[r->vector] : "Unknown";

    kerr("cpu", "EXCEPTION %u: %s", r->vector, name);
    kerr("cpu", "err=%08x eip=%08x cs=%04x eflags=%08x", r->err, r->eip, r->cs, r->eflags);
    kerr("cpu", "eax=%08x ebx=%08x ecx=%08x edx=%08x", r->eax, r->ebx, r->ecx, r->edx);
    kerr("cpu", "esi=%08x edi=%08x ebp=%08x", r->esi, r->edi, r->ebp);

    if (r->vector == 14) {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kerr("cpu", "cr2=%08x (faulting address)", cr2);
    }

    kernel_panic(name);
}