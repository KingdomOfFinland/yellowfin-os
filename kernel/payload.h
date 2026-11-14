#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdint.h>

/* Disk layout of a Yellowfin image: LBA 0 boot sector, LBA 1..8 stage2, LBA 9.. kernel.
   Must match boot.asm / stage2.asm (KERNEL_LBA 9, stage2 padded to 4096 bytes). */
#define PAYLOAD_STAGE2_SECTORS 8
#define PAYLOAD_KERNEL_LBA     9

/* Call this FIRST in kernel_main, before anything can change .data. It copies the boot
   sector, stage2 and the kernel image out of memory so the installer can write pristine copies. */
void payload_snapshot(void);

int  payload_ok(void);                    /* 1 if a boot sector was found and everything fit */
const uint8_t *payload_boot(void);        /* 512 bytes */
const uint8_t *payload_stage2(void);      /* PAYLOAD_STAGE2_SECTORS * 512 bytes */
const uint8_t *payload_kernel(void);
uint32_t payload_kernel_sectors(void);    /* sectors to write, includes one spare */

#endif