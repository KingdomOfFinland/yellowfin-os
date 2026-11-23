/*
 * http.c - minimal HTTP/1.0 client, plain or over TLS
 * YellowfinOS
 *
 * http_get()      plain HTTP, same signature as before
 * http_get_ex()   same, with a use_tls flag
 * http_get_url()  takes "http://host[:port]/path" or "https://host[:port]/path"
 */

#include "http.h"
#include "tcp.h"
#include "tls/tls.h"
#include "dns.h"
#include "ipv4.h"

#define REQ_MAX 2048

/* Not in http.h yet: add  #define HTTP_ERR_TLS  <next free value>  there and
   a message for it in the httpget command, then delete this fallback. */
#ifndef HTTP_ERR_TLS
#define HTTP_ERR_TLS  -20           /* TLS handshake or certificate failure */
#endif

static char req[REQ_MAX];
static u8   chunk[2048];

/* One set of functions per transport, so the GET code is written once. */
typedef struct {
    int  (*connect)(const char *host, const u8 *ip, u16 port);
    int  (*send)(const void *data, u32 length);
    int  (*recv)(void *buf, u16 capacity, u32 timeout_ms);
    void (*close)(void);
} io_t;

static int plain_connect(const char *host, const u8 *ip, u16 port)
{
    (void)host;
    return tcp_connect(ip, port, 1000);
}

static int tls_conn(const char *host, const u8 *ip, u16 port)
{
    return tls_connect(host, ip, port);
}

static const io_t io_plain = { plain_connect, tcp_send, tcp_recv, tcp_close };
static const io_t io_tls   = { tls_conn,      tls_send, tls_recv, tls_close };

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

int http_get_ex(const char *host, u16 port, const char *path, int use_tls,
                http_sink_t sink, u32 *total)
{
    const io_t *io = use_tls ? &io_tls : &io_plain;
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

    r = io->connect(host, ip, port);
    if (r != TCP_OK) {
        if (r == TLS_ERR_HANDSHAKE || r == TLS_ERR_CERT || r == TLS_ERR_INIT)
            return HTTP_ERR_TLS;
        return r == TCP_ERR_RESET ? HTTP_ERR_REFUSED : HTTP_ERR_CONNECT;
    }

    if (io->send(req, (u32)n) != TCP_OK) {
        io->close();
        return HTTP_ERR_SEND;
    }

    for (;;) {
        r = io->recv(chunk, sizeof(chunk), 5000);

        if (r > 0) {
            got += (u32)r;
            if (sink)
                sink(chunk, (u16)r);
        } else if (r == 0) {
            break;                          /* the server closed: done */
        } else {
            io->close();
            if (total)
                *total = got;
            return r == TCP_ERR_RESET ? HTTP_ERR_RESET : HTTP_ERR_TIMEOUT;
        }
    }

    io->close();
    if (total)
        *total = got;
    return HTTP_OK;
}

int http_get(const char *host, u16 port, const char *path,
             http_sink_t sink, u32 *total)
{
    return http_get_ex(host, port, path, 0, sink, total);
}

/* "http://host[:port]/path", "https://host[:port]/path", or just "host[/path]"
   (plain HTTP). The path defaults to "/". */
int http_get_url(const char *url, http_sink_t sink, u32 *total)
{
    char host[128];
    char path[REQ_MAX - 128];
    int  use_tls = 0;
    u32  port = 80;
    int  i = 0, h = 0, p = 0;

    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        if (url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/') {
            use_tls = 1;
            port = 443;
            i = 8;
        } else if (url[4] == ':' && url[5] == '/' && url[6] == '/') {
            i = 7;
        }
    }

    while (url[i] && url[i] != ':' && url[i] != '/' && h < (int)sizeof(host) - 1)
        host[h++] = url[i++];
    host[h] = 0;
    if (!h)
        return HTTP_ERR_REQUEST;

    if (url[i] == ':') {
        port = 0;
        i++;
        while (url[i] >= '0' && url[i] <= '9' && port < 65535)
            port = port * 10 + (u32)(url[i++] - '0');
        if (!port || port > 65535)
            return HTTP_ERR_REQUEST;
    }

    if (url[i] == '/') {
        while (url[i] && p < (int)sizeof(path) - 1)
            path[p++] = url[i++];
    } else {
        path[p++] = '/';
    }
    path[p] = 0;

    return http_get_ex(host, (u16)port, path, use_tls, sink, total);
}
