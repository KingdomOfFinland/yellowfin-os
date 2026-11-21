/*
 * dns.c - minimal DNS client (A records over UDP)
 * YellowfinOS
 */

#include "dns.h"
#include "udp.h"
#include "ethernet.h"

#define DNS_PORT      53
#define DNS_MAX       512
#define DNS_ATTEMPTS  2

static u8  server[4] = { 10, 0, 2, 3 };
static u16 id_seq;
static u16 next_port = 49152;

static struct {
    int active;
    int got;
    u16 id;
    u16 len;
    u8  data[DNS_MAX];
} rx;

void dns_set_server(const u8 ip[4]) { eth_copy(server, ip, 4); }
const u8 *dns_server(void)          { return server; }

/* Runs inside ethernet_poll(): only copies the answer. */
static void dns_rx(const u8 *src_ip, u16 src_port, const u8 *data, u16 len)
{
    if (!rx.active || rx.got)
        return;

    if (src_port != DNS_PORT || !eth_eq(src_ip, server, 4))
        return;

    if (len < 12 || len > DNS_MAX)
        return;

    if (eth_rd16(data) != rx.id)
        return;

    eth_copy(rx.data, data, len);
    rx.len = len;
    rx.got = 1;
}

static int name_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

/* Builds a standard recursive A query. Returns its length, or 0 if the name
   is invalid. */
static u16 build_query(const char *name, u16 id, u8 *q, u16 cap)
{
    u16 pos = 12;
    const char *p = name;

    if (!*p)
        return 0;

    eth_wr16(q + 0, id);
    eth_wr16(q + 2, 0x0100);        /* recursion desired */
    eth_wr16(q + 4, 1);             /* one question */
    eth_wr16(q + 6, 0);
    eth_wr16(q + 8, 0);
    eth_wr16(q + 10, 0);

    while (*p) {
        u16 start = pos++;
        u16 n = 0;

        if (pos - 12 > 253)
            return 0;

        while (*p && *p != '.') {
            if (!name_char_ok(*p) || n >= 63 || (u16)(pos + 6) >= cap)
                return 0;
            q[pos++] = (u8)*p++;
            n++;
        }

        if (n == 0)
            return 0;               /* empty label: ".." or leading dot */

        q[start] = (u8)n;

        if (*p == '.') {
            p++;
            if (!*p)
                break;              /* a trailing dot is fine */
        }
    }

    if ((u16)(pos + 5) > cap)
        return 0;

    q[pos++] = 0;                   /* end of name */
    eth_wr16(q + pos, 1);           /* type A */
    pos += 2;
    eth_wr16(q + pos, 1);           /* class IN */
    pos += 2;

    return pos;
}

/* Returns the offset just past a (possibly compressed) name, or -1. */
static int skip_name(const u8 *m, int len, int pos)
{
    while (pos < len) {
        u8 c = m[pos];

        if (c == 0)
            return pos + 1;

        if ((c & 0xC0) == 0xC0) {   /* compression pointer ends the name */
            if (pos + 1 >= len)
                return -1;
            return pos + 2;
        }

        if (c & 0xC0)
            return -1;

        pos += 1 + c;
    }

    return -1;
}

static int parse_response(const u8 *m, int len, u8 ip_out[4])
{
    u16 flags = eth_rd16(m + 2);
    u16 qd = eth_rd16(m + 4);
    u16 an = eth_rd16(m + 6);
    int pos = 12;
    u16 i;

    if (!(flags & 0x8000))
        return DNS_ERR_SERVER;      /* not a response */

    switch (flags & 0x000F) {
    case 0:  break;
    case 3:  return DNS_ERR_NXDOMAIN;
    default: return DNS_ERR_SERVER;
    }

    for (i = 0; i < qd; i++) {
        pos = skip_name(m, len, pos);
        if (pos < 0)
            return DNS_ERR_SERVER;
        pos += 4;                   /* type + class */
        if (pos > len)
            return DNS_ERR_SERVER;
    }

    /* Walk the answers; skip CNAMEs and anything else until an A record. */
    for (i = 0; i < an; i++) {
        u16 type, cls, rdlen;

        pos = skip_name(m, len, pos);
        if (pos < 0 || pos + 10 > len)
            break;

        type  = eth_rd16(m + pos);
        cls   = eth_rd16(m + pos + 2);
        rdlen = eth_rd16(m + pos + 8);
        pos += 10;

        if (pos + rdlen > len)
            break;

        if (type == 1 && cls == 1 && rdlen == 4) {
            eth_copy(ip_out, m + pos, 4);
            return DNS_OK;
        }

        pos += rdlen;
    }

    return DNS_ERR_NOADDR;
}

int dns_resolve(const char *name, u8 ip_out[4], u32 timeout_ms)
{
    u8  q[300];
    u16 qlen, id, port;
    int attempt;

    if (!name || !ip_out)
        return DNS_ERR_NAME;

    /* A handler can't wait for packets: rx_buf is in use. */
    if (ethernet_busy())
        return DNS_ERR_NET;

    id = (u16)(net_now_ms() * 31u + (u32)(++id_seq));
    qlen = build_query(name, id, q, (u16)sizeof(q));
    if (!qlen)
        return DNS_ERR_NAME;

    port = next_port++;
    if (next_port > 65000)
        next_port = 49152;

    if (!udp_bind(port, dns_rx))
        return DNS_ERR_NET;

    rx.id = id;
    rx.got = 0;
    rx.active = 1;

    for (attempt = 0; attempt < DNS_ATTEMPTS; attempt++) {
        u32 start;

        if (udp_send(server, port, DNS_PORT, q, qlen) <= 0) {
            rx.active = 0;
            udp_unbind(port);
            return DNS_ERR_NET;
        }

        start = net_now_ms();

        while ((u32)(net_now_ms() - start) < timeout_ms) {
            ethernet_poll();

            if (rx.got) {
                int r = parse_response(rx.data, rx.len, ip_out);

                rx.active = 0;
                udp_unbind(port);
                return r;
            }
        }
    }

    rx.active = 0;
    udp_unbind(port);
    return DNS_ERR_TIMEOUT;
}
