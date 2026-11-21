/*
 * tcp.c - TCP client, one connection
 * YellowfinOS
 */

#include "tcp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"

#define TCP_HLEN    20
#define TCP_MSS     1460
#define TCP_RXBUF   32768
#define TCP_RTO_MS  1000
#define TCP_TRIES   6

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

/* Sequence number comparison that survives 32-bit wraparound. */
#define SEQ_LE(a, b) ((int)((u32)(a) - (u32)(b)) <= 0)

enum {
    S_CLOSED, S_SYN_SENT, S_ESTABLISHED, S_FIN_WAIT_1,
    S_FIN_WAIT_2, S_CLOSE_WAIT, S_CLOSING, S_LAST_ACK
};

tcp_stats_t tcp_stats;

static struct {
    int state;
    u8  peer_ip[4];
    u8  peer_mac[6];
    u16 local_port;
    u16 peer_port;
    u32 snd_una;            /* oldest unacknowledged sequence number */
    u32 snd_nxt;            /* next sequence number to send */
    u32 rcv_nxt;            /* next sequence number we expect */
    u16 peer_mss;
    u16 mss_out;
    u16 peer_wnd;
    int reset;
    int fin_rcvd;
    u16 rx_head, rx_tail, rx_count;
    u8  rxbuf[TCP_RXBUF];
} c;

static u8  seg[TCP_HLEN + 4 + TCP_MSS];
static u16 next_port = 40000;
static u32 iss_counter;

static u32 rd32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

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

/* Pseudo-header + segment checksum. A received segment with a valid checksum
   gives 0. */
static u16 tcp_cksum(const u8 *src, const u8 *dst, const u8 *t, u16 len)
{
    u32 sum = 0;

    sum = sum_bytes(src, 4, sum);
    sum = sum_bytes(dst, 4, sum);
    sum += IP_PROTO_TCP;
    sum += len;
    sum = sum_bytes(t, len, sum);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (u16)~sum;
}

static u16 rcv_window(void)
{
    return (u16)(TCP_RXBUF - c.rx_count);
}

/* Builds and sends one segment. Independent of the connection state, so it
   also serves for RSTs to unknown peers. */
static void emit(const u8 *dst_mac, const u8 *dst_ip, u16 sport, u16 dport,
                 u32 seq, u32 ack, u8 flags, u16 wnd, int with_mss,
                 const u8 *data, u16 len)
{
    u16 hl = with_mss ? 24 : 20;
    u16 total = (u16)(hl + len);

    if (len > TCP_MSS)
        return;

    eth_wr16(seg + 0, sport);
    eth_wr16(seg + 2, dport);
    wr32(seg + 4, seq);
    wr32(seg + 8, ack);
    seg[12] = (u8)((hl / 4) << 4);
    seg[13] = flags;
    eth_wr16(seg + 14, wnd);
    eth_wr16(seg + 16, 0);
    eth_wr16(seg + 18, 0);

    if (with_mss) {
        seg[20] = 2;                    /* option: maximum segment size */
        seg[21] = 4;
        eth_wr16(seg + 22, TCP_MSS);
    }

    if (len)
        eth_copy(seg + hl, data, len);

    eth_wr16(seg + 16, tcp_cksum(arp_local_ip(), dst_ip, seg, total));

    tcp_stats.tx_segments++;
    ipv4_send_mac(dst_mac, dst_ip, IP_PROTO_TCP, seg, total);
}

static void tcp_ack(void)
{
    emit(c.peer_mac, c.peer_ip, c.local_port, c.peer_port,
         c.snd_nxt, c.rcv_nxt, F_ACK, rcv_window(), 0, 0, 0);
}

static void send_fin(void)
{
    /* The FIN occupies the sequence number just before snd_nxt. */
    emit(c.peer_mac, c.peer_ip, c.local_port, c.peer_port,
         c.snd_nxt - 1, c.rcv_nxt, F_FIN | F_ACK, rcv_window(), 0, 0, 0);
}

static void send_rst_for(const u8 *mac, const u8 *ip, u16 sport, u16 dport,
                         u8 flags, u32 seq, u32 ack, u16 dlen)
{
    tcp_stats.resets_sent++;

    if (flags & F_ACK) {
        emit(mac, ip, dport, sport, ack, 0, F_RST, 0, 0, 0, 0);
    } else {
        u32 next = seq + dlen + ((flags & F_SYN) ? 1u : 0u)
                             + ((flags & F_FIN) ? 1u : 0u);
        emit(mac, ip, dport, sport, 0, next, F_RST | F_ACK, 0, 0, 0, 0);
    }
}

static void parse_mss(const u8 *opt, u16 len)
{
    u16 i = 0;

    while (i < len) {
        u8 kind = opt[i];

        if (kind == 0)
            break;
        if (kind == 1) {
            i++;
            continue;
        }
        if (i + 1 >= len || opt[i + 1] < 2)
            break;
        if (kind == 2 && opt[i + 1] == 4 && i + 3 < len)
            c.peer_mss = eth_rd16(opt + i + 2);
        i = (u16)(i + opt[i + 1]);
    }
}

/* ---- receive path: runs inside ethernet_poll(), never blocks ------------ */

void tcp_input(const u8 *ip, u16 hlen, u16 total, const u8 *src_mac)
{
    const u8 *t = ip + hlen;
    u16 tlen = (u16)(total - hlen);
    u16 sport, dport, doff, wnd, dlen;
    u32 seq, ack;
    u8 flags;
    const u8 *data;

    tcp_stats.rx_segments++;

    if (tlen < TCP_HLEN) {
        tcp_stats.bad_length++;
        return;
    }

    if (!eth_eq(ip + 16, arp_local_ip(), 4))
        return;

    if (tcp_cksum(ip + 12, ip + 16, t, tlen) != 0) {
        tcp_stats.bad_checksum++;
        return;
    }

    sport = eth_rd16(t);
    dport = eth_rd16(t + 2);
    seq   = rd32(t + 4);
    ack   = rd32(t + 8);
    doff  = (u16)((t[12] >> 4) * 4);
    flags = t[13];
    wnd   = eth_rd16(t + 14);

    if (doff < TCP_HLEN || doff > tlen) {
        tcp_stats.bad_length++;
        return;
    }

    data = t + doff;
    dlen = (u16)(tlen - doff);

    /* Not our connection: answer with a reset (never reset a reset). */
    if (c.state == S_CLOSED || dport != c.local_port ||
        sport != c.peer_port || !eth_eq(ip + 12, c.peer_ip, 4)) {
        if (!(flags & F_RST))
            send_rst_for(src_mac, ip + 12, sport, dport, flags, seq, ack, dlen);
        return;
    }

    if (flags & F_RST) {
        if (c.state == S_SYN_SENT) {
            if ((flags & F_ACK) && ack == c.snd_nxt) {
                c.reset = 1;
                c.state = S_CLOSED;
                tcp_stats.resets_rx++;
            }
        } else if (seq == c.rcv_nxt) {
            c.reset = 1;
            c.state = S_CLOSED;
            tcp_stats.resets_rx++;
        }
        return;
    }

    if (c.state == S_SYN_SENT) {
        if ((flags & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == c.snd_nxt) {
            c.rcv_nxt = seq + 1;
            c.snd_una = ack;
            c.peer_wnd = wnd;
            c.peer_mss = 536;
            parse_mss(t + TCP_HLEN, (u16)(doff - TCP_HLEN));
            c.mss_out = c.peer_mss < TCP_MSS ? c.peer_mss : TCP_MSS;
            c.state = S_ESTABLISHED;
            tcp_ack();
        }
        return;
    }

    /* Synchronized states: first the ACK field ... */
    if (flags & F_ACK) {
        if (SEQ_LE(c.snd_una, ack) && SEQ_LE(ack, c.snd_nxt)) {
            c.snd_una = ack;
            c.peer_wnd = wnd;
        }

        if (c.snd_una == c.snd_nxt) {       /* everything, our FIN included */
            if (c.state == S_FIN_WAIT_1) {
                c.state = S_FIN_WAIT_2;
            } else if (c.state == S_CLOSING || c.state == S_LAST_ACK) {
                c.state = S_CLOSED;
                return;
            }
        }
    }

    /* ... then data and FIN. Pure ACKs need no answer. */
    if (dlen > 0 || (flags & (F_FIN | F_SYN))) {
        int can_rx = (c.state == S_ESTABLISHED ||
                      c.state == S_FIN_WAIT_1 ||
                      c.state == S_FIN_WAIT_2);
        int skip = (int)(c.rcv_nxt - seq);

        /* Part of this segment is old (a retransmission): trim it. */
        if (skip > 0 && (u16)skip <= dlen) {
            data += skip;
            dlen = (u16)(dlen - skip);
            seq = c.rcv_nxt;
        }

        if (seq == c.rcv_nxt && can_rx) {
            u16 room = rcv_window();
            u16 take = dlen < room ? dlen : room;
            u16 i;

            for (i = 0; i < take; i++) {
                c.rxbuf[c.rx_tail] = data[i];
                c.rx_tail = (u16)((c.rx_tail + 1) % TCP_RXBUF);
                c.rx_count++;
            }
            c.rcv_nxt += take;

            if ((flags & F_FIN) && take == dlen) {
                c.rcv_nxt++;
                c.fin_rcvd = 1;

                if (c.state == S_ESTABLISHED)
                    c.state = S_CLOSE_WAIT;
                else if (c.state == S_FIN_WAIT_1)
                    c.state = S_CLOSING;
                else if (c.state == S_FIN_WAIT_2)
                    c.state = S_CLOSED;     /* no TIME_WAIT */
            }
        }

        /* Always answer: new data, duplicates and out-of-order segments
           (the latter act as a duplicate ACK that makes the peer resend). */
        tcp_ack();
    }
}

/* ---- API: runs outside handlers, polls while it waits ------------------- */

int tcp_connect(const u8 ip[4], u16 port, u32 attempt_ms)
{
    u8  mac[6];
    u32 iss;
    int attempt;

    if (ethernet_busy())
        return TCP_ERR_NET;
    if (c.state != S_CLOSED)
        return TCP_ERR_BUSY;
    if (!ipv4_route(ip, mac))
        return TCP_ERR_NET;

    eth_copy(c.peer_ip, ip, 4);
    eth_copy(c.peer_mac, mac, 6);
    c.peer_port = port;
    c.local_port = next_port++;
    if (next_port > 60000)
        next_port = 40000;

    c.reset = 0;
    c.fin_rcvd = 0;
    c.rx_head = 0;
    c.rx_tail = 0;
    c.rx_count = 0;
    c.peer_mss = 536;
    c.mss_out = 536;
    c.peer_wnd = 0;
    c.rcv_nxt = 0;

    iss = net_now_ms() * 2503u + 0x1234567u * (++iss_counter);
    c.snd_una = iss;
    c.snd_nxt = iss + 1;
    c.state = S_SYN_SENT;

    for (attempt = 0; attempt < 3; attempt++) {
        u32 start;

        emit(c.peer_mac, c.peer_ip, c.local_port, c.peer_port,
             iss, 0, F_SYN, rcv_window(), 1, 0, 0);
        if (attempt)
            tcp_stats.retransmits++;

        start = net_now_ms();
        while ((u32)(net_now_ms() - start) < attempt_ms) {
            ethernet_poll();

            if (c.state == S_ESTABLISHED)
                return TCP_OK;
            if (c.state == S_CLOSED)
                return c.reset ? TCP_ERR_RESET : TCP_ERR_NET;
        }
    }

    c.state = S_CLOSED;
    return TCP_ERR_TIMEOUT;
}

static int tcp_fail(void)
{
    if (c.state != S_CLOSED) {
        emit(c.peer_mac, c.peer_ip, c.local_port, c.peer_port,
             c.snd_nxt, c.rcv_nxt, F_RST | F_ACK, 0, 0, 0, 0);
        c.state = S_CLOSED;
    }
    return TCP_ERR_TIMEOUT;
}

int tcp_send(const void *data, u32 length)
{
    const u8 *p = (const u8 *)data;

    while (length) {
        u16 n = length > c.mss_out ? c.mss_out : (u16)length;
        u32 seq, start;
        int tries;

        if (c.state != S_ESTABLISHED && c.state != S_CLOSE_WAIT)
            return c.reset ? TCP_ERR_RESET : TCP_ERR_NET;

        /* Respect the peer's receive window. */
        start = net_now_ms();
        while (c.peer_wnd == 0) {
            ethernet_poll();
            if (c.state != S_ESTABLISHED && c.state != S_CLOSE_WAIT)
                return c.reset ? TCP_ERR_RESET : TCP_ERR_NET;
            if ((u32)(net_now_ms() - start) > 5000)
                return tcp_fail();
        }
        if (n > c.peer_wnd)
            n = c.peer_wnd;

        seq = c.snd_nxt;
        c.snd_nxt = seq + n;

        for (tries = 0; tries < TCP_TRIES; tries++) {
            emit(c.peer_mac, c.peer_ip, c.local_port, c.peer_port,
                 seq, c.rcv_nxt, F_PSH | F_ACK, rcv_window(), 0, p, n);
            if (tries)
                tcp_stats.retransmits++;

            start = net_now_ms();
            while ((u32)(net_now_ms() - start) < TCP_RTO_MS) {
                ethernet_poll();

                if (c.state == S_CLOSED)
                    return c.reset ? TCP_ERR_RESET : TCP_ERR_NET;
                if (SEQ_LE(c.snd_nxt, c.snd_una))
                    break;
            }

            if (SEQ_LE(c.snd_nxt, c.snd_una))
                break;
        }

        if (!SEQ_LE(c.snd_nxt, c.snd_una))
            return tcp_fail();

        p += n;
        length -= n;
    }

    return TCP_OK;
}

int tcp_recv(void *buf, u16 capacity, u32 timeout_ms)
{
    u32 start = net_now_ms();

    for (;;) {
        if (c.rx_count) {
            u16 before = rcv_window();
            u16 n = 0;
            u8 *o = (u8 *)buf;

            while (n < capacity && c.rx_count) {
                o[n++] = c.rxbuf[c.rx_head];
                c.rx_head = (u16)((c.rx_head + 1) % TCP_RXBUF);
                c.rx_count--;
            }

            /* The window was nearly closed and reopened: tell the peer. */
            if (before < TCP_MSS && rcv_window() >= TCP_MSS &&
                (c.state == S_ESTABLISHED || c.state == S_FIN_WAIT_1 ||
                 c.state == S_FIN_WAIT_2))
                tcp_ack();

            return n;
        }

        if (c.reset)
            return TCP_ERR_RESET;
        if (c.fin_rcvd || c.state == S_CLOSED)
            return 0;                       /* EOF */
        if ((u32)(net_now_ms() - start) >= timeout_ms)
            return TCP_ERR_TIMEOUT;

        ethernet_poll();
    }
}

void tcp_close(void)
{
    u32 start, last;

    if (c.state == S_ESTABLISHED) {
        c.snd_nxt++;                        /* the FIN takes one sequence number */
        c.state = S_FIN_WAIT_1;
    } else if (c.state == S_CLOSE_WAIT) {
        c.snd_nxt++;
        c.state = S_LAST_ACK;
    } else {
        c.state = S_CLOSED;                 /* SYN_SENT or already closing */
        return;
    }

    send_fin();
    start = last = net_now_ms();

    while (c.state != S_CLOSED && (u32)(net_now_ms() - start) < 4000) {
        ethernet_poll();

        if ((c.state == S_FIN_WAIT_1 || c.state == S_CLOSING ||
             c.state == S_LAST_ACK) &&
            (u32)(net_now_ms() - last) >= TCP_RTO_MS) {
            send_fin();
            last = net_now_ms();
        }
    }

    if (c.state != S_CLOSED) {
        emit(c.peer_mac, c.peer_ip, c.local_port, c.peer_port,
             c.snd_nxt, c.rcv_nxt, F_RST | F_ACK, 0, 0, 0, 0);
        c.state = S_CLOSED;
    }
}
