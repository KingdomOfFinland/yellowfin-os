
#ifndef YF_GM45_PCI_H
#define YF_GM45_PCI_H

#include <stdint.h>

/* YellowfinOS: Intel GM45 graphics device information. */

#define YF_PCI_VENDOR_INTEL       0x8086

/*
 * Common GM45 graphics device ID.
 * Verify against the actual PCI ID reported by YellowfinOS.
 * Do not assume every GM45 system uses the same ID.
 */
#define YF_PCI_DEVICE_GM45        0x2A42

#define YF_GM45_GENERATION        4
#define YF_GM45_PIPE_COUNT        2
#define YF_GM45_HAS_PIPE_A        1
#define YF_GM45_HAS_PIPE_B        1

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  revision_id;
    uint32_t bar0;
    uint32_t bar0_size;
    uint8_t  detected;
} yf_gm45_device_t;

static inline int yf_gm45_is_intel(uint16_t vendor_id)
{
    return vendor_id == YF_PCI_VENDOR_INTEL;
}

static inline int yf_gm45_is_supported_id(uint16_t vendor_id,
                                          uint16_t device_id)
{
    return vendor_id == YF_PCI_VENDOR_INTEL &&
           device_id == YF_PCI_DEVICE_GM45;
}

#endif
