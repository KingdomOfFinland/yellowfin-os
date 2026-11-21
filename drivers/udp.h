#ifndef UDP_H
#define UDP_H

#include "e1000.h"

#define UDP_HLEN         8
#define UDP_MAX_PAYLOAD  1472       /* 1500 - 20 (IP) - 8 (UDP) */

typedef struct {
    u32 rx_total;
    u32 rx_delivered;
    u32 bad_length;
    u32 bad_checksum;
    u32 no_listener;
    u32 tx_total;
} udp_stats_t;

extern udp_stats_t udp_stats;

/* Runs INSIDE ethernet_poll()'s dispatch: it may copy data, but must not
   block, call arp_resolve() or send through the ARP path. The data pointer is
   only valid during the call. */
typedef void (*udp_handler_t)(const u8 *src_ip, u16 src_port,
                              const u8 *data, u16 length);

/* Returns 1 on success, 0 if the port is taken or the table is full. */
int  udp_bind(u16 port, udp_handler_t handler);
void udp_unbind(u16 port);

/* Returns 1 on success, 0 if the send failed, -1 if there was no route
   (same convention as ipv4_send). */
int  udp_send(const u8 dst_ip[4], u16 src_port, u16 dst_port,
              const void *data, u16 length);

/* Called by ipv4_input() for protocol 17. ip points at the IPv4 header. */
void udp_input(const u8 *ip, u16 hlen, u16 total);

#endif