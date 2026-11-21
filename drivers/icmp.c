/*
 * icmp.c - ICMP echo (ping)
 * YellowfinOS
 */

#include "icmp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"

#define ICMP_ECHO_REPLY    0
#define ICMP_ECHO_REQUEST  8
#define ICMP_HLEN          8
#define ICMP_MAX           1480     /* 1500 - 20-byte IP header */
#define PING_DATA_LEN      32

icmp_stats_t icmp_stats;

static u8  reply_buf[ICMP_MAX];
static u16 ping_id = 0x5946;        /* "YF" */

static struct {
    int active;
    int got;
    u8  dst[4];
    u16 id;
    u16 seq;
    u8  ttl;
} pending;

void icmp_input(const u8 *ip, u16 hlen, u16 total, const u8 *src_mac)
{
    const u8 *icmp = ip + hlen;
    u16 len = (u16)(total - hlen);

    if (len < ICMP_HLEN) {
        icmp_stats.bad_length++;
        return;
    }

    /* Unicast to us only: never answer broadcast pings. */
    if (!eth_eq(ip + 16, arp_local_ip(), 4))
        return;

    if (inet_checksum(icmp, len) != 0) {
        icmp_stats.bad_checksum++;
        return;
    }

    switch (icmp[0]) {
    case ICMP_ECHO_REQUEST:
        if (icmp[1] != 0 || len > ICMP_MAX)
            return;

        icmp_stats.rx_echo_req++;

        /* Reply is the request with type 0 and a fresh checksum. It goes
           straight to the sender's MAC, so no ARP (and no recursion into
           arp_resolve from inside a frame handler). */
        eth_copy(reply_buf, icmp, len);
        reply_buf[0] = ICMP_ECHO_REPLY;
        eth_wr16(reply_buf + 2, 0);
        eth_wr16(reply_buf + 2, inet_checksum(reply_buf, len));

        if (ipv4_send_mac(src_mac, ip + 12, IP_PROTO_ICMP, reply_buf, len))
            icmp_stats.tx_echo_reply++;
        break;

    case ICMP_ECHO_REPLY:
        if (pending.active && !pending.got &&
            icmp[1] == 0 &&
            eth_eq(ip + 12, pending.dst, 4) &&
            eth_rd16(icmp + 4) == pending.id &&
            eth_rd16(icmp + 6) == pending.seq) {

            pending.got = 1;
            pending.ttl = ip[8];
            icmp_stats.rx_echo_reply++;
        } else {
            icmp_stats.rx_unmatched++;
        }
        break;

    default:
        icmp_stats.rx_other++;
        break;
    }
}

int icmp_ping(const u8 dst[4], u16 seq, u32 timeout_ms, u32 *rtt_ms, u8 *ttl)
{
    u8  msg[ICMP_HLEN + PING_DATA_LEN];
    u8  mac[6];
    u32 start;
    int i;

    /* Resolve the next hop first so ARP time isn't counted in the RTT. */
    if (!ipv4_route(dst, mac))
        return ICMP_ERR_NOROUTE;

    msg[0] = ICMP_ECHO_REQUEST;
    msg[1] = 0;
    eth_wr16(msg + 2, 0);
    eth_wr16(msg + 4, ping_id);
    eth_wr16(msg + 6, seq);

    for (i = 0; i < PING_DATA_LEN; i++)
        msg[ICMP_HLEN + i] = (u8)('a' + (i % 23));

    eth_wr16(msg + 2, inet_checksum(msg, sizeof(msg)));

    eth_copy(pending.dst, dst, 4);
    pending.id     = ping_id;
    pending.seq    = seq;
    pending.got    = 0;
    pending.active = 1;

    start = net_now_ms();

    if (!ipv4_send_mac(mac, dst, IP_PROTO_ICMP, msg, sizeof(msg))) {
        pending.active = 0;
        return ICMP_ERR_SEND;
    }

    while ((u32)(net_now_ms() - start) < timeout_ms) {
        ethernet_poll();

        if (pending.got) {
            if (rtt_ms) *rtt_ms = net_now_ms() - start;
            if (ttl)    *ttl = pending.ttl;
            pending.active = 0;
            return ICMP_OK;
        }
    }

    pending.active = 0;
    return ICMP_ERR_TIMEOUT;
}