/*
 * udp.c - UDP
 * YellowfinOS
 */

#include "udp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"

#define UDP_MAX_BINDINGS 4

udp_stats_t udp_stats;

static struct {
    u16 port;
    udp_handler_t handler;
} bindings[UDP_MAX_BINDINGS];

static u8 tx_dgram[UDP_HLEN + UDP_MAX_PAYLOAD];

static u32 sum_bytes(const u8 *p, u16 len, u32 sum)
{
    while (len > 1) {
        sum += (u32)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }

    if (len)
        sum += (u32)(p[0] << 8);

    return sum;
}

/* Checksum over the IPv4 pseudo-header plus the UDP datagram (header and
   data). Returns the complemented sum: a received datagram with a valid
   checksum gives 0. */
static u16 udp_cksum(const u8 *src, const u8 *dst, const u8 *udp, u16 len)
{
    u32 sum = 0;

    sum = sum_bytes(src, 4, sum);
    sum = sum_bytes(dst, 4, sum);
    sum += IP_PROTO_UDP;
    sum += len;
    sum = sum_bytes(udp, len, sum);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (u16)~sum;
}

int udp_bind(u16 port, udp_handler_t handler)
{
    int i;

    if (!port || !handler)
        return 0;

    for (i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].handler && bindings[i].port == port)
            return 0;
    }

    for (i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (!bindings[i].handler) {
            bindings[i].port = port;
            bindings[i].handler = handler;
            return 1;
        }
    }

    return 0;
}

void udp_unbind(u16 port)
{
    int i;

    for (i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].handler && bindings[i].port == port)
            bindings[i].handler = 0;
    }
}

void udp_input(const u8 *ip, u16 hlen, u16 total)
{
    const u8 *u = ip + hlen;
    u16 avail = (u16)(total - hlen);
    u16 ulen, dport;
    int i;

    udp_stats.rx_total++;

    if (avail < UDP_HLEN) {
        udp_stats.bad_length++;
        return;
    }

    ulen = eth_rd16(u + 4);
    if (ulen < UDP_HLEN || ulen > avail) {
        udp_stats.bad_length++;
        return;
    }

    /* A zero checksum means "not computed" and is legal in IPv4. */
    if (eth_rd16(u + 6) != 0 && udp_cksum(ip + 12, ip + 16, u, ulen) != 0) {
        udp_stats.bad_checksum++;
        return;
    }

    /* Unicast to us only (broadcasts such as DHCP are not handled yet). */
    if (!eth_eq(ip + 16, arp_local_ip(), 4))
        return;

    dport = eth_rd16(u + 2);

    for (i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].handler && bindings[i].port == dport) {
            udp_stats.rx_delivered++;
            bindings[i].handler(ip + 12, eth_rd16(u),
                                u + UDP_HLEN, (u16)(ulen - UDP_HLEN));
            return;
        }
    }

    udp_stats.no_listener++;
}

int udp_send(const u8 dst_ip[4], u16 src_port, u16 dst_port,
             const void *data, u16 length)
{
    u16 total = (u16)(UDP_HLEN + length);
    u16 ck;

    if (length > UDP_MAX_PAYLOAD)
        return 0;

    eth_wr16(tx_dgram + 0, src_port);
    eth_wr16(tx_dgram + 2, dst_port);
    eth_wr16(tx_dgram + 4, total);
    eth_wr16(tx_dgram + 6, 0);
    eth_copy(tx_dgram + UDP_HLEN, (const u8 *)data, length);

    ck = udp_cksum(arp_local_ip(), dst_ip, tx_dgram, total);
    if (ck == 0)
        ck = 0xFFFF;                /* 0 would mean "no checksum" */
    eth_wr16(tx_dgram + 6, ck);

    udp_stats.tx_total++;

    return ipv4_send(dst_ip, IP_PROTO_UDP, tx_dgram, total);
}