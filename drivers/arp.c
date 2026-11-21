/*
 * arp.c - Address Resolution Protocol
 * YellowfinOS
 *
 * Receives nothing by itself: ethernet.c hands over frames via arp_input().
 */

#include "arp.h"
#include "ethernet.h"

#define ARP_PKT_LEN     28
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#define ARP_CACHE_SIZE  8
#define ARP_RETRIES     3
#define ARP_RETRY_MS    500

struct arp_cache_entry {
    u8 ip[4];
    u8 mac[6];
    int valid;
};

static struct arp_cache_entry cache[ARP_CACHE_SIZE];
static u8 cache_next;
static u8 local_ip[4];

static const u8 unknown_mac[6] = { 0, 0, 0, 0, 0, 0 };

void arp_init(const u8 ip[4])
{
    int i;

    for (i = 0; i < ARP_CACHE_SIZE; i++)
        cache[i].valid = 0;

    cache_next = 0;

    if (ip)
        eth_copy(local_ip, ip, 4);
    else
        eth_copy(local_ip, unknown_mac, 4);     /* 0.0.0.0 */
}

const u8 *arp_local_ip(void)
{
    return local_ip;
}

static struct arp_cache_entry *cache_find(const u8 *ip)
{
    int i;

    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && eth_eq(cache[i].ip, ip, 4))
            return &cache[i];
    }

    return 0;
}

/*
 * Always refresh an existing entry. Only create a new one when the packet was
 * addressed to us, so other hosts' broadcasts can't flush the 8-entry cache.
 */
static void cache_update(const u8 *ip, const u8 *mac, int allow_insert)
{
    struct arp_cache_entry *e = cache_find(ip);

    if (!e) {
        if (!allow_insert)
            return;

        e = &cache[cache_next];
        cache_next = (u8)((cache_next + 1) % ARP_CACHE_SIZE);
        eth_copy(e->ip, ip, 4);
        e->valid = 1;
    }

    eth_copy(e->mac, mac, 6);
}

int arp_lookup(const u8 ip[4], u8 mac_out[6])
{
    struct arp_cache_entry *e = cache_find(ip);

    if (!e)
        return 0;

    eth_copy(mac_out, e->mac, 6);
    return 1;
}

static int arp_send(u16 opcode, const u8 *dest_mac,
                    const u8 *target_mac, const u8 *target_ip)
{
    u8 p[ARP_PKT_LEN];
    u8 mac[6];

    if (!ethernet_get_mac(mac))
        return 0;

    eth_wr16(p + 0, 1);                 /* hardware type: Ethernet */
    eth_wr16(p + 2, ETHERTYPE_IPV4);    /* protocol type: IPv4 */
    p[4] = 6;                           /* hardware address length */
    p[5] = 4;                           /* protocol address length */
    eth_wr16(p + 6, opcode);
    eth_copy(p + 8,  mac, 6);           /* sender MAC */
    eth_copy(p + 14, local_ip, 4);      /* sender IP */
    eth_copy(p + 18, target_mac, 6);    /* target MAC */
    eth_copy(p + 24, target_ip, 4);     /* target IP */

    return ethernet_send(dest_mac, ETHERTYPE_ARP, p, ARP_PKT_LEN);
}

/* frame is the whole Ethernet frame, already length- and MAC-filtered. */
void arp_input(const u8 *frame, u16 length)
{
    const u8 *a;
    const u8 *sender_mac;
    const u8 *sender_ip;
    const u8 *target_ip;
    u16 opcode;
    int for_us;

    if (length < ETH_HLEN + ARP_PKT_LEN)
        return;

    a = frame + ETH_HLEN;

    if (eth_rd16(a) != 1 || eth_rd16(a + 2) != ETHERTYPE_IPV4)
        return;

    if (a[4] != 6 || a[5] != 4)
        return;

    opcode = eth_rd16(a + 6);
    if (opcode != ARP_OP_REQUEST && opcode != ARP_OP_REPLY)
        return;

    sender_mac = a + 8;
    sender_ip  = a + 14;
    target_ip  = a + 24;

    /* Ignore invalid zero sender addresses. */
    if (!(sender_ip[0] | sender_ip[1] | sender_ip[2] | sender_ip[3]))
        return;

    for_us = eth_eq(target_ip, local_ip, 4);

    cache_update(sender_ip, sender_mac, for_us);

    if (opcode == ARP_OP_REQUEST && for_us)
        arp_send(ARP_OP_REPLY, sender_mac, sender_mac, sender_ip);
}

int arp_poll(void)
{
    return ethernet_poll();
}

int arp_resolve(const u8 ip[4], u8 mac_out[6])
{
    int attempt;

    if (!ip || !mac_out || !e1000_is_ready())
        return 0;

    if (arp_lookup(ip, mac_out))
        return 1;

    /* Inside a frame handler ethernet_poll() refuses to run, so waiting
       here could never succeed. */
    if (ethernet_busy())
        return 0;

    for (attempt = 0; attempt < ARP_RETRIES; attempt++) {
        u32 start;

        if (!arp_send(ARP_OP_REQUEST, eth_broadcast, unknown_mac, ip))
            return 0;

        start = net_now_ms();

        while ((u32)(net_now_ms() - start) < ARP_RETRY_MS) {
            ethernet_poll();            /* the single receive path */

            if (arp_lookup(ip, mac_out))
                return 1;
        }
    }

    return 0;
}