#include "pci.h"
#include "klog.h"

static u32 pci_address(u8 bus, u8 slot, u8 function, u8 offset)
{
    return 0x80000000u
        | ((u32)bus << 16)
        | ((u32)slot << 11)
        | ((u32)function << 8)
        | (offset & 0xFC);
}

static u32 pci_read32(u8 bus, u8 slot, u8 function, u8 offset)
{
    u32 address = pci_address(bus, slot, function, offset);
    u32 value;

    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"((u16)0xCF8));
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"((u16)0xCFC));

    return value;
}

u32 pci_read_config(u8 bus, u8 slot, u8 function, u8 offset)
{
    return pci_read32(bus, slot, function, offset);
}

int pci_find_device(u16 vendor_id, u16 device_id,
                    struct pci_device *out)
{
    u16 bus;
    u8 slot;
    u8 function;

    if (!out)
        return 0;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            u32 id0 = pci_read32((u8)bus, slot, 0, 0);

            if ((id0 & 0xFFFF) == 0xFFFF)
                continue;

            for (function = 0; function < 8; function++) {
                u32 id, classreg, header;

                if (function != 0) {
                    id = pci_read32((u8)bus, slot, function, 0);
                    if ((id & 0xFFFF) == 0xFFFF)
                        continue;
                } else {
                    id = id0;
                }

                if ((id & 0xFFFF) != vendor_id ||
                    ((id >> 16) & 0xFFFF) != device_id)
                    continue;

                classreg = pci_read32((u8)bus, slot, function, 8);
                header = pci_read32((u8)bus, slot, function, 0x0C);

                out->bus = (u8)bus;
                out->slot = slot;
                out->function = function;
                out->vendor_id = (u16)(id & 0xFFFF);
                out->device_id = (u16)(id >> 16);
                out->class_code = (u8)(classreg >> 24);
                out->subclass = (u8)(classreg >> 16);
                out->prog_if = (u8)(classreg >> 8);
                out->header_type = (u8)(header >> 16);

                return 1;
            }
        }
    }

    return 0;
}

u16 pci_read16(u8 b, u8 s, u8 f, u8 off) {
    return (u16)(pci_read32(b, s, f, off & 0xFC) >> ((off & 2) * 8));
}
u8 pci_read8(u8 b, u8 s, u8 f, u8 off) {
    return (u8)(pci_read32(b, s, f, off & 0xFC) >> ((off & 3) * 8));
}

void pci_write32(u8 b, u8 s, u8 f, u8 off, u32 v) {
    u32 a = pci_address(b, s, f, off);
    __asm__ volatile ("outl %0, %1" : : "a"(a), "Nd"((u16)0xCF8));
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"((u16)0xCFC));
}

void pci_write16(u8 b, u8 s, u8 f, u8 off, u16 v) {
    u32 old = pci_read32(b, s, f, off & 0xFC);
    int sh = (off & 2) * 8;
    old = (old & ~(0xFFFFu << sh)) | ((u32)v << sh);
    pci_write32(b, s, f, off & 0xFC, old);
}

void pci_enable(u8 b, u8 s, u8 f, u16 bits) {
    pci_write16(b, s, f, 0x04, pci_read16(b, s, f, 0x04) | bits);
}

// Size a BAR: decode is switched off while probing, then everything is restored.
u32 pci_bar_size(u8 b, u8 s, u8 f, int n) {
    u8 off = 0x10 + n * 4;
    u16 cmd = pci_read16(b, s, f, 0x04);
    u32 orig = pci_read32(b, s, f, off), mask, size;

    pci_write16(b, s, f, 0x04, cmd & ~3u);
    pci_write32(b, s, f, off, 0xFFFFFFFFu);
    mask = pci_read32(b, s, f, off);
    pci_write32(b, s, f, off, orig);
    pci_write16(b, s, f, 0x04, cmd);

    if (orig & 1) mask &= 0xFFFFFFFCu;   // I/O BAR
    else          mask &= 0xFFFFFFF0u;   // memory BAR
    if (!mask) return 0;
    size = ~mask + 1;
    return size;
}

void pci_dump_ide(u8 b, u8 s, u8 f) {
    u8 pi = (u8)(pci_read32(b, s, f, 8) >> 8);
    int n;
    // swap kinfo for whatever your klog call looks like
    kinfo("pci", "%x:%x.%x prog-if %x cmd %x", b, s, f, pi, pci_read16(b, s, f, 4));
    for (n = 0; n < 5; n++)
        kinfo("pci", "  bar%d %x", n, pci_read32(b, s, f, 0x10 + n * 4));
    kinfo("pci", "  ide_tim %x %x", pci_read16(b, s, f, 0x40), pci_read16(b, s, f, 0x42));
}