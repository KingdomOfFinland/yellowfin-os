/*
 * YellowfinOS Intel display bring-up / diagnostics
 * Target: legacy Intel Gen4/Gen4.5 integrated graphics (GM45 family).
 *
 * Native Yellowfin code; this is NOT Linux DRM/i915 source.
 *
 * IMPORTANT:
 * - PCI probing and register reads only.
 * - Does not program PLLs, panel power, pipes, planes, or GTT.
 * - Preserves the BIOS-configured display state.
 *
 * Integrate:
 * 1. Add this file to your kernel build.
 * 2. Add the prototypes to your kernel source.
 * 3. Call yf_intel_display_probe() after mem_init() and klog_probe_pci().
 */

#include <stdint.h>
#include "klog.h"
#include "plane.h"

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFu
#endif

#define YF_BIT(n)              (1u << (n))

#define PCI_ADDR_PORT          0x0CF8
#define PCI_DATA_PORT          0x0CFC
#define PCI_VENDOR             0x00
#define PCI_DEVICE             0x02
#define PCI_COMMAND            0x04
#define PCI_BAR0               0x10
#define PCI_CLASS_REVISION     0x08

#define PCI_VENDOR_INTEL        0x8086
#define PCI_COMMAND_MEMORY      YF_BIT(1)

#define INTEL_REG_DPLL_A        0x06014u
#define INTEL_REG_FP0_A         0x06040u
#define INTEL_REG_PIPEACONF     0x70008u
#define INTEL_REG_PIPEBCONF     0x71008u
#define INTEL_REG_DSPACNTR      0x70180u
#define INTEL_REG_DSPASTRIDE    0x70188u
#define INTEL_REG_DSPASURF      0x7019Cu
#define INTEL_REG_LVDS          0x61180u
#define INTEL_REG_PP_STATUS     0x61200u
#define INTEL_REG_PP_CONTROL    0x61204u

#define INTEL_PIPE_ENABLE       YF_BIT(31)
#define INTEL_PLANE_ENABLE      YF_BIT(31)
#define INTEL_LVDS_ENABLE       YF_BIT(31)

struct yf_intel_gpu {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;

    uint32_t mmio_phys;
    volatile uint8_t *mmio;

    int found;
    int mmio_valid;
};

static struct yf_intel_gpu yf_igpu;

/* ---------------------------------------------------------
 * PCI configuration I/O
 * --------------------------------------------------------- */

static inline void yf_outl(uint16_t port, uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint32_t yf_inl(uint16_t port)
{
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void yf_io_wait(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

static uint32_t yf_pci_read32(
    uint8_t bus,
    uint8_t dev,
    uint8_t fn,
    uint8_t offset)
{
    uint32_t address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)dev << 11) |
        ((uint32_t)fn << 8) |
        ((uint32_t)offset & 0xFCu);

    yf_outl(PCI_ADDR_PORT, address);
    yf_io_wait();

    return yf_inl(PCI_DATA_PORT);
}

static uint16_t yf_pci_read16(
    uint8_t bus,
    uint8_t dev,
    uint8_t fn,
    uint8_t offset)
{
    uint32_t value;

    value = yf_pci_read32(bus, dev, fn, offset);

    return (uint16_t)(
        value >> ((offset & 2u) * 8u)
    );
}

static void yf_pci_write16(
    uint8_t bus,
    uint8_t dev,
    uint8_t fn,
    uint8_t offset,
    uint16_t value)
{
    uint32_t old_value;
    uint32_t shift;
    uint32_t address;

    old_value = yf_pci_read32(
        bus, dev, fn, offset
    );

    shift = (uint32_t)(offset & 2u) * 8u;

    old_value =
        (old_value & ~(0xFFFFu << shift)) |
        ((uint32_t)value << shift);

    address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)dev << 11) |
        ((uint32_t)fn << 8) |
        ((uint32_t)offset & 0xFCu);

    yf_outl(PCI_ADDR_PORT, address);
    yf_io_wait();

    yf_outl(PCI_DATA_PORT, old_value);
    yf_io_wait();
}

/* ---------------------------------------------------------
 * Supported legacy Intel graphics IDs
 * --------------------------------------------------------- */

static int yf_intel_legacy_gpu_id(uint16_t id)
{
    switch (id) {
    case 0x2A42: /* Mobile GM45 / GMA 4500MHD */
    case 0x2A43:
    case 0x2E02:
    case 0x2E12:
    case 0x2E13:
    case 0x2E22:
    case 0x2E23:
        return 1;

    default:
        return 0;
    }
}

/* ---------------------------------------------------------
 * Graphics MMIO access
 * --------------------------------------------------------- */

static uint32_t yf_intel_mmio_read(uint32_t offset)
{
    volatile uint32_t *reg;

    if (!yf_igpu.mmio_valid)
        return 0xFFFFFFFFu;

    if ((offset & 3u) != 0)
        return 0xFFFFFFFFu;

    if (offset >= 0x80000u)
        return 0xFFFFFFFFu;

    reg = (volatile uint32_t *)(
        yf_igpu.mmio + offset
    );

    return *reg;
}

/* ---------------------------------------------------------
 * Probe Intel graphics and inspect firmware display state.
 *
 * Returns:
 *   1 = supported GPU detected
 *   0 = no supported GPU found
 *
 * NOTE:
 * This does not initialize native graphics output.
 * --------------------------------------------------------- */

int yf_intel_display_probe(void)
{
    unsigned int bus;
    unsigned int dev;
    unsigned int fn;

    uint32_t bar;
    uint32_t classrev;

    uint16_t command;

    yf_igpu.found = 0;
    yf_igpu.mmio_valid = 0;
    yf_igpu.mmio_phys = 0;
    yf_igpu.mmio = (volatile uint8_t *)0;
	
	    {
        struct yf_plane_info pi;
        if (yf_plane_read((uintptr_t)yf_igpu.mmio, 0, &pi) == 0)
            yf_plane_log("A", &pi);
        if (yf_plane_read((uintptr_t)yf_igpu.mmio, 1, &pi) == 0)
            yf_plane_log("B", &pi);
    }

    kinfo(
        "intel",
        "probing PCI for legacy Intel graphics"
    );

    for (bus = 0; bus < 256; ++bus) {
        for (dev = 0; dev < 32; ++dev) {
            uint16_t vendor0;
            uint8_t max_fn;

            vendor0 = yf_pci_read16(
                (uint8_t)bus,
                (uint8_t)dev,
                0,
                PCI_VENDOR
            );

            if (vendor0 == 0xFFFFu)
                continue;

            max_fn =
                (yf_pci_read32(
                    (uint8_t)bus,
                    (uint8_t)dev,
                    0,
                    0x0C
                ) & 0x00800000u) ? 8 : 1;

            for (fn = 0; fn < max_fn; ++fn) {
                uint16_t vendor;
                uint16_t id;

                vendor = yf_pci_read16(
                    (uint8_t)bus,
                    (uint8_t)dev,
                    (uint8_t)fn,
                    PCI_VENDOR
                );

                if (vendor != PCI_VENDOR_INTEL)
                    continue;

                id = yf_pci_read16(
                    (uint8_t)bus,
                    (uint8_t)dev,
                    (uint8_t)fn,
                    PCI_DEVICE
                );

                if (!yf_intel_legacy_gpu_id(id))
                    continue;

                classrev = yf_pci_read32(
                    (uint8_t)bus,
                    (uint8_t)dev,
                    (uint8_t)fn,
                    PCI_CLASS_REVISION
                );

                yf_igpu.bus = (uint8_t)bus;
                yf_igpu.device = (uint8_t)dev;
                yf_igpu.function = (uint8_t)fn;

                yf_igpu.vendor_id = vendor;
                yf_igpu.device_id = id;

                yf_igpu.class_code =
                    (uint8_t)(classrev >> 24);

                yf_igpu.subclass =
                    (uint8_t)(classrev >> 16);

                yf_igpu.found = 1;

                break;
            }

            if (yf_igpu.found)
                break;
        }

        if (yf_igpu.found)
            break;
    }

    if (!yf_igpu.found) {
        kwarn(
            "intel",
            "no supported Intel graphics device found"
        );

        return 0;
    }

    kinfo(
        "intel",
        "GPU at %u:%u.%u vendor=%04x device=%04x class=%02x:%02x",
        yf_igpu.bus,
        yf_igpu.device,
        yf_igpu.function,
        yf_igpu.vendor_id,
        yf_igpu.device_id,
        yf_igpu.class_code,
        yf_igpu.subclass
    );

    /* Enable PCI memory decoding if needed. */
    command = yf_pci_read16(
        yf_igpu.bus,
        yf_igpu.device,
        yf_igpu.function,
        PCI_COMMAND
    );

    if (!(command & PCI_COMMAND_MEMORY)) {
        yf_pci_write16(
            yf_igpu.bus,
            yf_igpu.device,
            yf_igpu.function,
            PCI_COMMAND,
            (uint16_t)(
                command | PCI_COMMAND_MEMORY
            )
        );

        kdebug(
            "intel",
            "enabled PCI memory decoding"
        );
    }

    bar = yf_pci_read32(
        yf_igpu.bus,
        yf_igpu.device,
        yf_igpu.function,
        PCI_BAR0
    );

       bar = yf_pci_read32(
        yf_igpu.bus,
        yf_igpu.device,
        yf_igpu.function,
        PCI_BAR0
    );

    /* Reject an unset BAR or an I/O BAR; accept 32-bit and low 64-bit BARs. */
    if (bar == 0 || bar == UINT32_MAX || (bar & 1u)) {
        kwarn("intel", "graphics BAR0 unavailable (%08x)", bar);
        return 1;
    }
    if (((bar >> 1) & 3u) == 2u) {
        uint32_t hi = yf_pci_read32(yf_igpu.bus, yf_igpu.device,
                                    yf_igpu.function, PCI_BAR0 + 4);
        if (hi != 0) {
            kwarn("intel", "BAR0 above 4 GiB (%08x:%08x)", hi, bar);
            return 1;
        }
    }

    yf_igpu.mmio_phys = bar & ~0xFu;     /* this line stays as it is */

    /*
     * Assumes physical memory is directly addressable.
     * This must be replaced with the kernel's physical mapper
     * if Yellowfin enables paging without identity mapping.
     */
    yf_igpu.mmio = (volatile uint8_t *)(
        (uintptr_t)yf_igpu.mmio_phys
    );

    yf_igpu.mmio_valid = 1;

    kinfo(
        "intel",
        "graphics MMIO BAR0=%08x (direct-map assumption)",
        yf_igpu.mmio_phys
    );

    /* Read-only diagnostics. No display-register writes. */

    kdebug(
        "intel",
        "DPLL_A=%08x FP0_A=%08x",
        yf_intel_mmio_read(INTEL_REG_DPLL_A),
        yf_intel_mmio_read(INTEL_REG_FP0_A)
    );

    kdebug(
        "intel",
        "PIPE A=%08x PIPE B=%08x",
        yf_intel_mmio_read(INTEL_REG_PIPEACONF),
        yf_intel_mmio_read(INTEL_REG_PIPEBCONF)
    );

    kdebug(
        "intel",
        "plane A ctrl=%08x stride=%08x surface=%08x",
        yf_intel_mmio_read(INTEL_REG_DSPACNTR),
        yf_intel_mmio_read(INTEL_REG_DSPASTRIDE),
        yf_intel_mmio_read(INTEL_REG_DSPASURF)
    );

    kdebug(
        "intel",
        "LVDS=%08x panel-power=%08x/%08x",
        yf_intel_mmio_read(INTEL_REG_LVDS),
        yf_intel_mmio_read(INTEL_REG_PP_STATUS),
        yf_intel_mmio_read(INTEL_REG_PP_CONTROL)
    );

    if (yf_intel_mmio_read(INTEL_REG_PIPEACONF) &
        INTEL_PIPE_ENABLE) {

        kinfo(
            "intel",
            "pipe A already enabled by firmware"
        );
    }

    if (yf_intel_mmio_read(INTEL_REG_DSPACNTR) &
        INTEL_PLANE_ENABLE) {

        kinfo(
            "intel",
            "primary plane A already enabled by firmware"
        );
    }

    if (yf_intel_mmio_read(INTEL_REG_LVDS) &
        INTEL_LVDS_ENABLE) {

        kinfo(
            "intel",
            "LVDS port appears enabled"
        );
    }

    kinfo(
        "intel",
        "probe complete; firmware display state preserved"
    );

    return 1;
}

/* ---------------------------------------------------------
 * Print the last detected Intel GPU's status.
 * --------------------------------------------------------- */

void yf_intel_display_status(void)
{
    if (!yf_igpu.found) {
        kwarn(
            "intel",
            "no Intel GPU probe result; run yf_intel_display_probe at boot"
        );

        return;
    }

    kinfo(
        "intel",
        "status PCI %u:%u.%u vendor=%04x device=%04x MMIO=%08x",
        yf_igpu.bus,
        yf_igpu.device,
        yf_igpu.function,
        yf_igpu.vendor_id,
        yf_igpu.device_id,
        yf_igpu.mmio_phys
    );

    if (yf_igpu.mmio_valid) {
        kinfo(
            "intel",
            "PIPEA=%08x DSPACNTR=%08x LVDS=%08x",
            yf_intel_mmio_read(INTEL_REG_PIPEACONF),
            yf_intel_mmio_read(INTEL_REG_DSPACNTR),
            yf_intel_mmio_read(INTEL_REG_LVDS)
        );
    }
}