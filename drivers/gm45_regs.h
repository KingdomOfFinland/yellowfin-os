
#ifndef YELLOWFIN_GM45_REGS_H
#define YELLOWFIN_GM45_REGS_H

#include <stdint.h>

/*
 * YellowfinOS GM45 display registers
 * Inspired by Intel i915 register definitions.
 *
 * This header does not initialize the GPU.
 * Set the MMIO base only after validating PCI BAR0.
 */

#define GM45_PCI_VENDOR_ID  0x8086
#define GM45_PCI_DEVICE_ID  0x2A42

/* Display register offsets */
#define GM45_PIPEACONF      0x70008
#define GM45_PIPEBCONF      0x71008

#define GM45_DSPACNTR       0x70180
#define GM45_DSPBCNTR       0x71180

#define GM45_DSPAADDR       0x70184
#define GM45_DSPBADDR       0x71184

#define GM45_DSPASTRIDE     0x70188
#define GM45_DSPBSTRIDE     0x71188

#define GM45_DSPASIZE       0x70190
#define GM45_DSPBSIZE       0x71190

#define GM45_DISPLAY_PLANE_ENABLE (1u << 31)

/* GM45 register access */
static inline uint32_t gm45_read32(uintptr_t mmio_base,
                                   uint32_t reg)
{
    volatile uint32_t *address =
        (volatile uint32_t *)(mmio_base + reg);

    return *address;
}

#endif /* YELLOWFIN_GM45_REGS_H */
