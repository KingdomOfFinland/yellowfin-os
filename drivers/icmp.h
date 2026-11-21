#ifndef ICMP_H
#define ICMP_H

#include "e1000.h"

#define ICMP_OK            0
#define ICMP_ERR_TIMEOUT  -1
#define ICMP_ERR_NOROUTE  -2    /* ARP for the next hop failed */
#define ICMP_ERR_SEND     -3

typedef struct {
    u32 rx_echo_req;        /* echo requests received */
    u32 tx_echo_reply;      /* echo replies we sent */
    u32 rx_echo_reply;      /* replies that matched our pending ping */
    u32 rx_unmatched;       /* echo replies we weren't waiting for */
    u32 bad_checksum;
    u32 bad_length;
    u32 rx_other;           /* other ICMP types: ignored */
} icmp_stats_t;

extern icmp_stats_t icmp_stats;

/* Called by ipv4_input() for protocol 1. ip points at the IPv4 header,
   src_mac is the sender's MAC from the Ethernet header (used to reply
   without ARP). */
void icmp_input(const u8 *ip, u16 hlen, u16 total, const u8 *src_mac);

/* Sends one echo request and polls ethernet_poll() until the matching reply
   arrives or timeout_ms passes. Returns ICMP_OK or an ICMP_ERR_* code.
   rtt_ms has 10 ms granularity (100 Hz timer). */
int icmp_ping(const u8 dst[4], u16 seq, u32 timeout_ms, u32 *rtt_ms, u8 *ttl);

#endif