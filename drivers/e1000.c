
#include "e1000.h"

int e1000_detect(struct pci_device *out)
{
    /*
     * QEMU's common 82540EM emulation:
     * Intel vendor 8086, device 100E.
     */
    return pci_find_device(0x8086, 0x100E, out);
}
