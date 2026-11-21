/*
 * ipv4.c - IPv4 input validation
 * YellowfinOS
 */

#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"

ipv4_stats_t ipv4_stats;

u16 inet_checksum(const void *data, u16 length)
{
    const u8 *p = (const u8 *)data;
    u32 sum = 0;

    while (length > 1) {
        sum += (u32)((p[0] << 8) | p[1]);
        p += 2;
        length -= 2;
    }

    if (length)
        sum += (u32)(p[0] << 8);        /* odd byte, padded with zero */

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (u16)~sum;
}

static int is_broadcast(const u8 *ip)
{
    return (ip[0] & ip[1] & ip[2] & ip[3]) == 0xFF;
}

/* ---- configuration, routing and transmit ------------------------------- */

static u8 net_mask[4] = { 255, 255, 255, 0 };
static u8 net_gw[4]   = { 10, 0, 2, 2 };
static u8 tx_pkt[1500];
static u16 ip_id;

void ipv4_set_config(const u8 mask[4], const u8 gateway[4])
{
    eth_copy(net_mask, mask, 4);
    eth_copy(net_gw, gateway, 4);
}

int ipv4_parse(const char *s, u8 out[4])
{
    int part, digits;
    u32 v;

    for (part = 0; part < 4; part++) {
        v = 0;
        digits = 0;

        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (u32)(*s - '0');
            if (v > 255)
                return 0;
            s++;
            digits++;
        }

        if (!digits)
            return 0;

        out[part] = (u8)v;

        if (part < 3) {
            if (*s != '.')
                return 0;
            s++;
        }
    }

    while (*s == ' ')
        s++;

    return *s == 0;
}

static int on_link(const u8 *dst)
{
    const u8 *me = arp_local_ip();
    int i;

    for (i = 0; i < 4; i++) {
        if ((dst[i] & net_mask[i]) != (me[i] & net_mask[i]))
            return 0;
    }

    return 1;
}

int ipv4_route(const u8 dst[4], u8 mac_out[6])
{
    if (is_broadcast(dst)) {
        eth_copy(mac_out, eth_broadcast, 6);
        return 1;
    }

    return arp_resolve(on_link(dst) ? dst : net_gw, mac_out);
}

int ipv4_send_mac(const u8 dst_mac[6], const u8 dst[4], u8 proto,
                  const void *payload, u16 length)
{
    u16 total = (u16)(IPV4_HLEN_MIN + length);

    if (total > sizeof(tx_pkt))
        return 0;

    tx_pkt[0] = 0x45;                   /* version 4, IHL 5 */
    tx_pkt[1] = 0;                      /* TOS */
    eth_wr16(tx_pkt + 2, total);
    eth_wr16(tx_pkt + 4, ip_id++);
    eth_wr16(tx_pkt + 6, 0x4000);       /* DF, no fragmentation */
    tx_pkt[8] = 64;                     /* TTL */
    tx_pkt[9] = proto;
    eth_wr16(tx_pkt + 10, 0);
    eth_copy(tx_pkt + 12, arp_local_ip(), 4);
    eth_copy(tx_pkt + 16, dst, 4);
    eth_wr16(tx_pkt + 10, inet_checksum(tx_pkt, IPV4_HLEN_MIN));
    eth_copy(tx_pkt + IPV4_HLEN_MIN, (const u8 *)payload, length);

    return ethernet_send(dst_mac, ETHERTYPE_IPV4, tx_pkt, total);
}

int ipv4_send(const u8 dst[4], u8 proto, const void *payload, u16 length)
{
    u8 mac[6];

    if (!ipv4_route(dst, mac))
        return -1;

    return ipv4_send_mac(mac, dst, proto, payload, length);
}

void ipv4_input(const u8 *frame, u16 length)
{
    const u8 *ip;
    u16 hlen, total;

    ipv4_stats.rx_total++;

    if (length < ETH_HLEN + IPV4_HLEN_MIN) {
        ipv4_stats.bad_length++;
        return;
    }

    ip = frame + ETH_HLEN;

    if ((ip[0] >> 4) != 4) {
        ipv4_stats.bad_header++;
        return;
    }

    hlen = (u16)((ip[0] & 0x0F) * 4);
    if (hlen < IPV4_HLEN_MIN) {
        ipv4_stats.bad_header++;
        return;
    }

    /* Trust total_length, not the frame length: Ethernet pads short frames
       to 60 bytes, so the frame can be longer than the IP packet. */
    total = eth_rd16(ip + 2);
    if (total < hlen || total > (u16)(length - ETH_HLEN)) {
        ipv4_stats.bad_length++;
        return;
    }

    if (inet_checksum(ip, hlen) != 0) {
        ipv4_stats.bad_checksum++;
        return;
    }

    /* No reassembly: drop MF packets and non-zero fragment offsets. */
    if (eth_rd16(ip + 6) & 0x3FFF) {
        ipv4_stats.fragments++;
        return;
    }

    /* Destination: our address or limited broadcast. */
    if (!eth_eq(ip + 16, arp_local_ip(), 4) && !is_broadcast(ip + 16)) {
        ipv4_stats.not_for_us++;
        return;
    }

    ipv4_stats.rx_ok++;

    switch (ip[9]) {
        case IP_PROTO_ICMP:
            ipv4_stats.rx_icmp++;
            icmp_input(ip, hlen, total, frame + 6);
            break;

        case IP_PROTO_UDP:
            udp_input(ip, hlen, total);
            break;
			
		case IP_PROTO_TCP:
			tcp_input(ip, hlen, total, frame + 6);
			break;

        default:
            ipv4_stats.rx_other++;
            break;
    }
}

/* ---- self-test --------------------------------------------------------- */

static void st_seal(u8 *f)
{
    eth_wr16(f + 24, 0);
    eth_wr16(f + 24, inet_checksum(f + 14, 20));
}

/* A valid 28-byte ICMP-protocol packet from 10.0.2.2, zero-padded to 64. */
static void st_build(u8 *f, const u8 *dst)
{
    int i;

    for (i = 0; i < 64; i++)
        f[i] = 0;

    f[12] = 0x08; f[13] = 0x00;         /* EtherType: IPv4 */
    f[14] = 0x45;                       /* version 4, IHL 5 */
    eth_wr16(f + 16, 28);               /* total length: 20 + 8 */
    eth_wr16(f + 20, 0x4000);           /* DF */
    f[22] = 64;                         /* TTL */
    f[23] = IP_PROTO_ICMP;
    f[26] = 10; f[27] = 0; f[28] = 2; f[29] = 2;    /* src 10.0.2.2 */
    eth_copy(f + 30, dst, 4);
    st_seal(f);
}

#define ST_RUN(n, field, len) do {                          \
        u32 b_ = ipv4_stats.field;                          \
        ipv4_input(f, (len));                               \
        if (!fail && ipv4_stats.field != b_ + 1) fail = (n);\
    } while (0)

int ipv4_selftest(void)
{
   u32 saved[sizeof(ipv4_stats_t) / 4];
u32 *live = (u32 *)&ipv4_stats;

u32 saved_icmp[sizeof(icmp_stats_t) / 4];
u32 *live_icmp = (u32 *)&icmp_stats;

u8 f[64]; 
    u8 other[4] = { 10, 0, 2, 99 };
    int fail = 0;
    unsigned i;

    for (i = 0; i < sizeof(saved) / 4; i++)
        saved[i] = live[i];

    st_build(f, arp_local_ip());
    ST_RUN(1, rx_icmp, 60);             /* valid, Ethernet-padded */

    ST_RUN(2, rx_icmp, 42);             /* valid, exact length */

    st_build(f, eth_broadcast);
    ST_RUN(3, rx_icmp, 60);             /* limited broadcast destination */

    st_build(f, arp_local_ip());
    f[22] = 63;                         /* change TTL, don't re-seal */
    ST_RUN(4, bad_checksum, 60);

    st_build(f, arp_local_ip());
    f[14] = 0x65;                       /* version 6 */
    ST_RUN(5, bad_header, 60);

    st_build(f, arp_local_ip());
    f[14] = 0x44;                       /* IHL 4 */
    ST_RUN(6, bad_header, 60);

    st_build(f, arp_local_ip());
    ST_RUN(7, bad_length, 20);          /* truncated */

    st_build(f, arp_local_ip());
    eth_wr16(f + 16, 200);              /* claims more than the frame holds */
    st_seal(f);
    ST_RUN(8, bad_length, 60);

    st_build(f, other);
    ST_RUN(9, not_for_us, 60);

    st_build(f, arp_local_ip());
    eth_wr16(f + 20, 0x2000);           /* More Fragments */
    st_seal(f);
    ST_RUN(10, fragments, 60);

    st_build(f, arp_local_ip());
    f[23] = 99;                         /* unknown protocol */
    st_seal(f);
    ST_RUN(11, rx_other, 60);

    for (i = 0; i < sizeof(saved) / 4; i++)
    live[i] = saved[i];
	
for (i = 0; i < sizeof(saved_icmp) / 4; i++)
    saved_icmp[i] = live_icmp[i];

	for (i = 0; i < sizeof(saved_icmp) / 4; i++)
    live_icmp[i] = saved_icmp[i];
	


    return fail;
}