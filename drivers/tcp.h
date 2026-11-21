#ifndef TCP_H
#define TCP_H

#include "e1000.h"

#define TCP_OK            0
#define TCP_ERR_TIMEOUT  -1
#define TCP_ERR_RESET    -2     /* peer refused or reset the connection */
#define TCP_ERR_NET      -3     /* no route, ARP failed or bad state */
#define TCP_ERR_BUSY     -4     /* a connection is already open */

typedef struct {
    u32 rx_segments;
    u32 tx_segments;
    u32 retransmits;
    u32 resets_rx;
    u32 resets_sent;
    u32 bad_checksum;
    u32 bad_length;
} tcp_stats_t;

extern tcp_stats_t tcp_stats;

/* Opens a connection (3 attempts of attempt_ms each). Returns TCP_OK or an
   error. Resolves the next-hop MAC, so it may block in ARP first. */
int  tcp_connect(const u8 ip[4], u16 port, u32 attempt_ms);

/* Sends all the data (stop-and-wait). Returns TCP_OK or an error. */
int  tcp_send(const void *data, u32 length);

/* Waits up to timeout_ms for data. Returns bytes copied (>0), 0 when the peer
   has closed and everything was read (EOF), or a negative TCP_ERR_*. */
int  tcp_recv(void *buf, u16 capacity, u32 timeout_ms);

/* Closes politely (FIN), waits up to ~4 s, then resets. Always ends CLOSED. */
void tcp_close(void);

/* Called by ipv4_input() for protocol 6. src_mac is the sender's MAC from the
   Ethernet header (used to answer segments for unknown connections). */
void tcp_input(const u8 *ip, u16 hlen, u16 total, const u8 *src_mac);

#endif
