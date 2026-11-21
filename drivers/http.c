/*
 * http.c - minimal HTTP/1.0 client
 * YellowfinOS
 */

#include "http.h"
#include "tcp.h"
#include "dns.h"
#include "ipv4.h"

#define REQ_MAX 512

static char req[REQ_MAX];
static u8   chunk[1024];

/* Appends s to req at *n. Returns 0 if it doesn't fit. */
static int put(int *n, const char *s)
{
    while (*s) {
        if (*n >= REQ_MAX - 1)
            return 0;
        req[(*n)++] = *s++;
    }
    return 1;
}

int http_get(const char *host, u16 port, const char *path,
             http_sink_t sink, u32 *total)
{
    u8  ip[4];
    int n = 0;
    int r;
    u32 got = 0;

    if (total)
        *total = 0;

    if (!ipv4_parse(host, ip)) {
        if (dns_resolve(host, ip, 2000) != DNS_OK)
            return HTTP_ERR_DNS;
    }

    if (!put(&n, "GET ") || !put(&n, path) ||
        !put(&n, " HTTP/1.0\r\nHost: ") || !put(&n, host) ||
        !put(&n, "\r\nUser-Agent: YellowfinOS/0.3\r\n"
                 "Connection: close\r\n\r\n"))
        return HTTP_ERR_REQUEST;

    r = tcp_connect(ip, port, 1000);
    if (r != TCP_OK)
        return r == TCP_ERR_RESET ? HTTP_ERR_REFUSED : HTTP_ERR_CONNECT;

    if (tcp_send(req, (u32)n) != TCP_OK) {
        tcp_close();
        return HTTP_ERR_SEND;
    }

    for (;;) {
        r = tcp_recv(chunk, sizeof(chunk), 5000);

        if (r > 0) {
            got += (u32)r;
            if (sink)
                sink(chunk, (u16)r);
        } else if (r == 0) {
            break;                          /* the server closed: done */
        } else {
            tcp_close();
            if (total)
                *total = got;
            return r == TCP_ERR_RESET ? HTTP_ERR_RESET : HTTP_ERR_TIMEOUT;
        }
    }

    tcp_close();
    if (total)
        *total = got;
    return HTTP_OK;
}
