#ifndef HTTP_H
#define HTTP_H

#include "e1000.h"

#define HTTP_OK             0
#define HTTP_ERR_DNS       -1
#define HTTP_ERR_CONNECT   -2
#define HTTP_ERR_REFUSED   -3
#define HTTP_ERR_REQUEST   -4   /* host or path too long */
#define HTTP_ERR_SEND      -5
#define HTTP_ERR_TIMEOUT   -6
#define HTTP_ERR_RESET     -7

typedef void (*http_sink_t)(const u8 *data, u16 length);

/* HTTP/1.0 GET over plain TCP (no TLS). host is a name or a dotted IP.
   Every received chunk (status line and headers included) goes to sink.
   total gets the number of bytes received. Returns HTTP_OK or HTTP_ERR_*. */
int http_get(const char *host, u16 port, const char *path,
             http_sink_t sink, u32 *total);

#endif
