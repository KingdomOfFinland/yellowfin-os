#ifndef DNS_H
#define DNS_H

#include "e1000.h"

#define DNS_OK             0
#define DNS_ERR_TIMEOUT   -1
#define DNS_ERR_NXDOMAIN  -2    /* the name does not exist */
#define DNS_ERR_SERVER    -3    /* server failure or malformed answer */
#define DNS_ERR_NOADDR    -4    /* answer had no IPv4 (A) record */
#define DNS_ERR_NAME      -5    /* invalid name */
#define DNS_ERR_NET       -6    /* could not send (ARP or TX failure) */

/* DNS server, default 10.0.2.3 (QEMU's built-in resolver). */
void dns_set_server(const u8 ip[4]);
const u8 *dns_server(void);

/* Resolves name to an IPv4 address (A record). Blocks, polling
   ethernet_poll(), for up to 2 attempts of timeout_ms each. */
int  dns_resolve(const char *name, u8 ip_out[4], u32 timeout_ms);

#endif
