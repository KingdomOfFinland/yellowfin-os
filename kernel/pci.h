#ifndef PCI_H
#define PCI_H

#include "ftfs.h"   /* u8/u16/u32 come from here */

struct pci_device {
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 header_type;
};

int pci_find_device(u16 vendor_id, u16 device_id,
                    struct pci_device *out);

u32  pci_read_config(u8 bus, u8 slot, u8 function, u8 offset);

u8   pci_read8 (u8 bus, u8 slot, u8 fn, u8 off);
u16  pci_read16(u8 bus, u8 slot, u8 fn, u8 off);
void pci_write32(u8 bus, u8 slot, u8 fn, u8 off, u32 v);
void pci_write16(u8 bus, u8 slot, u8 fn, u8 off, u16 v);
void pci_enable(u8 bus, u8 slot, u8 fn, u16 bits); /* 1=IO 2=MEM 4=busmaster */
u32  pci_bar_size(u8 bus, u8 slot, u8 fn, int n);
void pci_dump_ide(u8 bus, u8 slot, u8 fn);

#endif