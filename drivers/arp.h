#ifndef ARP_H
#define ARP_H

#include "e1000.h"

void arp_init(const u8 ip[4]);
const u8 *arp_local_ip(void);

/* Called by ethernet.c with an already-received, MAC-filtered frame. */
void arp_input(const u8 *frame, u16 length);

/* Compatibility wrapper: forwards to ethernet_poll(). Returns frames handled. */
int  arp_poll(void);

/* Cache only, no network traffic. Returns 1 on hit. */
int  arp_lookup(const u8 ip[4], u8 mac_out[6]);

/* Cache, then request plus ethernet_poll() with retries.
   Returns 1 on success, 0 on failure. */
int  arp_resolve(const u8 ip[4], u8 mac_out[6]);

#endif