/*
 * ethernet.c - Ethernet layer: central RX dispatch and TX framing
 * YellowfinOS
 */

#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "timer.h"

#define NET_TICK_HZ 100         /* matches ARP_TIMEOUT 200 == 2 s in the old code */

eth_stats_t eth_stats;
const u8 eth_broadcast[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static u8  rx_buf[ETH_BUF_SIZE];
static u8  tx_buf[ETH_BUF_SIZE];
static u8  my_mac[ETH_ALEN];
static int mac_valid;
static int dispatching;

u32 net_now_ms(void)
{
    return timer_ticks() * (1000u / NET_TICK_HZ);
}

static int load_mac(void)
{
    if (!mac_valid) {
        if (!e1000_get_mac(my_mac))
            return 0;
        mac_valid = 1;
    }
    return 1;
}

int ethernet_get_mac(u8 mac[ETH_ALEN])
{
    if (!load_mac())
        return 0;
    eth_copy(mac, my_mac, ETH_ALEN);
    return 1;
}

int ethernet_busy(void)
{
    return dispatching;
}

static void ethernet_dispatch(const u8 *frame, u16 length)
{
    u16 type;

    eth_stats.rx_frames++;

    if (length < ETH_HLEN) {
        eth_stats.rx_short++;
        return;
    }

    if (!load_mac())
        return;

    if (!eth_eq(frame, eth_broadcast, ETH_ALEN) &&
        !eth_eq(frame, my_mac, ETH_ALEN)) {
        eth_stats.rx_filtered++;
        return;
    }

    type = eth_rd16(frame + 12);

    switch (type) {
    case ETHERTYPE_ARP:
        eth_stats.rx_arp++;
        arp_input(frame, length);
        break;

    case ETHERTYPE_IPV4:
        eth_stats.rx_ipv4++;
        ipv4_input(frame, length);
        break;

    default:
        eth_stats.rx_ignored++;     /* ignored silently */
        break;
    }
}

int ethernet_poll(void)
{
    int handled = 0;
    int i;

    /* rx_buf is still in use by the outer dispatch, so a handler must never
       trigger a second receive. */
    if (dispatching)
        return 0;

    if (!e1000_is_ready())
        return 0;

    dispatching = 1;

    for (i = 0; i < ETH_POLL_BUDGET; i++) {
        int n = e1000_receive(rx_buf, (u16)sizeof(rx_buf));

        if (n <= 0)
            break;

        handled++;
        ethernet_dispatch(rx_buf, (u16)n);
    }

    dispatching = 0;

    return handled;
}

int ethernet_send(const u8 *dst, u16 ethertype, const void *payload, u16 length)
{
    u16 total;

    if (!dst || (length && !payload))
        return 0;

    if (length > ETH_MAX_FRAME - ETH_HLEN)
        return 0;

    if (!load_mac())
        return 0;

    eth_copy(tx_buf, dst, ETH_ALEN);
    eth_copy(tx_buf + 6, my_mac, ETH_ALEN);
    eth_wr16(tx_buf + 12, ethertype);
    eth_copy(tx_buf + ETH_HLEN, (const u8 *)payload, length);

    total = (u16)(ETH_HLEN + length);
    while (total < ETH_ZLEN)
        tx_buf[total++] = 0;

    if (!e1000_send(tx_buf, total)) {
        eth_stats.tx_errors++;
        return 0;
    }

    eth_stats.tx_frames++;
    return 1;
}