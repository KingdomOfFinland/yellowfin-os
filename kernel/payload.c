/* payload.c - keeps pristine copies of what the installer must write to a new disk.

   Why a snapshot: the kernel at 1 MiB has already changed its own .data by the time the
   installer runs, so copying it live would install a kernel with mutated initial values.
   Taking the copy at the very start of kernel_main avoids that. The boot sector (0x7C00)
   and stage2 (0x8000) are still where the BIOS and the loader left them. */

#include "payload.h"

#define BOOT_ADDR    0x7C00u
#define STAGE2_ADDR  0x8000u
#define KERNEL_ADDR  0x100000u

#define STAGE2_BYTES (PAYLOAD_STAGE2_SECTORS * 512u)
#define KMAX         (512u * 1024u)       /* largest kernel image the installer can carry */

extern char __bss_start[];                /* linker.ld: end of .data = end of kernel.bin */

static uint8_t buf[512u + STAGE2_BYTES + KMAX] __attribute__((aligned(4)));
static uint32_t klen, ksec;
static int ok;

void payload_snapshot(void)
{
    const volatile uint8_t *boot = (const volatile uint8_t *)BOOT_ADDR;
    const volatile uint8_t *st2  = (const volatile uint8_t *)STAGE2_ADDR;
    const volatile uint8_t *kern = (const volatile uint8_t *)KERNEL_ADDR;
    uint32_t i;

    ok = 0;
    klen = (uint32_t)(uintptr_t)__bss_start - KERNEL_ADDR;

    /* booted with QEMU -kernel: there is no boot sector, nothing to install from */
    if (boot[510] != 0x55 || boot[511] != 0xAA)
        return;
    if (klen == 0 || klen + 1024u > KMAX)
        return;

    for (i = 0; i < 512; i++)
        buf[i] = boot[i];
    for (i = 0; i < STAGE2_BYTES; i++)
        buf[512 + i] = st2[i];
    for (i = 0; i < klen; i++)
        buf[512 + STAGE2_BYTES + i] = kern[i];

    /* stage2 keeps a status byte near the end of its 4 KiB (0x8FF0). Start the installed
       copy with that area clean so a stale "success" value can't be carried over. */
    for (i = STAGE2_BYTES - 16; i < STAGE2_BYTES; i++)
        buf[512 + i] = 0;

    ksec = (klen + 511u) / 512u + 1u;     /* +1 spare sector */
    ok = 1;
}

int payload_ok(void)                      { return ok; }
const uint8_t *payload_boot(void)         { return buf; }
const uint8_t *payload_stage2(void)       { return buf + 512; }
const uint8_t *payload_kernel(void)       { return buf + 512 + STAGE2_BYTES; }
uint32_t payload_kernel_sectors(void)     { return ksec; }