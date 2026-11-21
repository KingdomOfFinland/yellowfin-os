#ifndef IPV4_H
#define IPV4_H

#include "e1000.h"

#define IPV4_HLEN_MIN   20

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

typedef struct {
    u32 rx_total;
    u32 rx_ok;
    u32 bad_length;
    u32 bad_header;
    u32 bad_checksum;
    u32 not_for_us;
    u32 fragments;
    u32 rx_icmp;
    u32 rx_other;
} ipv4_stats_t;

extern ipv4_stats_t ipv4_stats;

u16 inet_checksum(const void *data, u16 length);

/* Subnet mask and default gateway (defaults: 255.255.255.0 and 10.0.2.2,
   the QEMU user-network values). The local IP comes from arp_init(). */
void ipv4_set_config(const u8 mask[4], const u8 gateway[4]);

/* "a.b.c.d" -> 4 bytes. Returns 1 on success. */
int  ipv4_parse(const char *s, u8 out[4]);

/* MAC of the next hop for dst: dst itself if on-link, otherwise the gateway.
   Uses arp_resolve(), so it may block. Returns 1 on success. */
int  ipv4_route(const u8 dst[4], u8 mac_out[6]);

/* Builds the IPv4 header and sends to an explicit MAC (no ARP).
   Returns 1 on success, 0 on failure. */
int  ipv4_send_mac(const u8 dst_mac[6], const u8 dst[4], u8 proto,
                   const void *payload, u16 length);

/* ipv4_route() + ipv4_send_mac(). Returns 1 ok, 0 send failed, -1 no route. */
int  ipv4_send(const u8 dst[4], u8 proto, const void *payload, u16 length);

void ipv4_input(const u8 *frame, u16 length);
int  ipv4_selftest(void);

#endif