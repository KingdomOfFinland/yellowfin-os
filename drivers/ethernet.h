#ifndef ETHERNET_H
#define ETHERNET_H

#include "e1000.h"              /* u8/u16/u32 and e1000_* */

#define ETH_ALEN         6
#define ETH_HLEN         14
#define ETH_ZLEN         60     /* minimum frame without CRC */
#define ETH_MAX_FRAME    1518
#define ETH_BUF_SIZE     2048
#define ETH_POLL_BUDGET  8      /* max frames handled per ethernet_poll() */

#define ETHERTYPE_IPV4   0x0800
#define ETHERTYPE_ARP    0x0806

typedef struct {
    u32 rx_frames;
    u32 rx_arp;
    u32 rx_ipv4;
    u32 rx_ignored;     /* unsupported EtherType (IPv6, VLAN, ...) */
    u32 rx_short;       /* shorter than an Ethernet header */
    u32 rx_filtered;    /* not our MAC and not broadcast */
    u32 tx_frames;
    u32 tx_errors;
} eth_stats_t;

extern eth_stats_t eth_stats;
extern const u8 eth_broadcast[ETH_ALEN];

/* Byte-wise helpers: no alignment or aliasing problems, no libc. */
static inline u16 eth_rd16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline void eth_wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static inline void eth_copy(u8 *d, const u8 *s, u16 n) { while (n--) *d++ = *s++; }
static inline int eth_eq(const u8 *a, const u8 *b, u16 n) {
    while (n--) { if (*a++ != *b++) return 0; }
    return 1;
}

/* 1 on success, 0 if the NIC has no MAC yet. */
int  ethernet_get_mac(u8 mac[ETH_ALEN]);

/* The ONLY caller of e1000_receive(). Returns frames handled.
   Returns 0 at once if called from inside a frame handler. */
int  ethernet_poll(void);

/* Non-zero while a received frame is being dispatched. */
int  ethernet_busy(void);

/* Builds the Ethernet header (src = our MAC), pads to 60 bytes and sends.
   Returns 1 on success, 0 on failure (same convention as e1000_send). */
int  ethernet_send(const u8 *dst, u16 ethertype, const void *payload, u16 length);

/* Milliseconds since boot (assumes a 100 Hz timer_ticks()). */
u32  net_now_ms(void);

#endif