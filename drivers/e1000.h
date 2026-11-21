#ifndef E1000_H
#define E1000_H

#include "pci.h"

int e1000_detect(struct pci_device *out);
int e1000_init(struct pci_device *dev);
void e1000_rx_debug(void);

int e1000_send(const void *data, u16 length);
int e1000_receive(void *buffer, u16 capacity);

int e1000_is_ready(void);
int e1000_get_mac(u8 mac[6]);

#endif
