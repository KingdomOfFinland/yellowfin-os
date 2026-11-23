/*
 * tls.c - TLS 1.2 / 1.3 client over tcp.c, using Mbed TLS
 * YellowfinOS
 *
 * One connection at a time, like tcp.c. Mbed TLS reads and writes through
 * tcp_recv() and tcp_send(), which block (while polling the NIC) until they
 * are done, so the handshake is a plain blocking call.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"        /* only for the MBEDTLS_ERR_NET_* values */
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/psa_util.h"
#include "psa/crypto.h"

#include "tls.h"

extern const unsigned int   yf_ca_count;
extern const unsigned short yf_ca_len[];
extern const unsigned char  yf_ca_der[];
extern void *yf_tls_heap(size_t *size);

#define TLS_IDLE_MS   30000u            /* wait for a record when asked for 0 */

static int  inited;
static int  ca_loaded;
static unsigned ca_roots;
static int  verify_mode = TLS_VERIFY_REQUIRED;
static int  active;

static mbedtls_ssl_context ssl;
static mbedtls_ssl_config  conf;
static mbedtls_x509_crt    ca;

static int      last_code;
static uint32_t last_flags;
static const char *ver_str = "";
static const char *suite_str = "";
static char     errbuf[160];

/* ---- Mbed TLS <-> tcp.c ---------------------------------------------------- */

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int r;

    (void)ctx;
    r = tcp_send(buf, (u32)len);
    if (r == TCP_OK)
        return (int)len;
    return r == TCP_ERR_RESET ? MBEDTLS_ERR_NET_CONN_RESET
                              : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv_timeout(void *ctx, unsigned char *buf, size_t len,
                            uint32_t timeout)
{
    int r;

    (void)ctx;
    if (len > 16384)
        len = 16384;

    r = tcp_recv(buf, (u16)len, timeout ? timeout : TLS_IDLE_MS);
    if (r >= 0)
        return r;                       /* bytes, or 0 = peer closed */
    if (r == TCP_ERR_TIMEOUT)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    return r == TCP_ERR_RESET ? MBEDTLS_ERR_NET_CONN_RESET
                              : MBEDTLS_ERR_NET_RECV_FAILED;
}

/* ---- setup ------------------------------------------------------------------ */

static int tls_init(void)
{
    size_t heap_size;
    void *heap;
    psa_status_t st;

    if (inited)
        return 0;

    heap = yf_tls_heap(&heap_size);
    mbedtls_memory_buffer_alloc_init((unsigned char *)heap, heap_size);

    st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        last_code = (int)st;
        return -1;
    }

    mbedtls_x509_crt_init(&ca);
    inited = 1;
    return 0;
}

static int load_roots(void)
{
    unsigned i, off = 0;

    if (ca_loaded)
        return 0;

    for (i = 0; i < yf_ca_count; i++) {
        /* A root we cannot parse (unsupported curve, say) is skipped; the
           others still work. _nocopy keeps the DER in .rodata, not the heap. */
        if (mbedtls_x509_crt_parse_der_nocopy(&ca, yf_ca_der + off,
                                              yf_ca_len[i]) == 0)
            ca_roots++;
        off += yf_ca_len[i];
    }

    ca_loaded = 1;
    return ca_roots ? 0 : -1;
}

/* ---- public API ------------------------------------------------------------- */

void tls_set_verify(int mode) { verify_mode = mode; }
int  tls_get_verify(void)     { return verify_mode; }
int  tls_last_code(void)      { return last_code; }

unsigned tls_ca_count(void)
{
    return ca_roots;
}

int tls_add_ca(const unsigned char *data, unsigned len)
{
    int r;

    if (tls_init() != 0)
        return last_code ? last_code : -1;
    load_roots();                       /* best effort: our own roots still work */

    r = mbedtls_x509_crt_parse(&ca, data, len);
    if (r >= 0)
        ca_roots++;
    return r < 0 ? r : 0;
}

const char *tls_version(void)     { return ver_str; }
const char *tls_ciphersuite(void) { return suite_str; }

const char *tls_last_error(void)
{
    size_t n;

    errbuf[0] = 0;

    if (last_code) {
        /* PSA status codes are small negatives, Mbed TLS ones are -0x1000.. */
        mbedtls_strerror(last_code, errbuf, sizeof(errbuf));
        n = strlen(errbuf);
    } else {
        n = 0;
    }

    if (last_flags) {
        const char *sep = n ? "; " : "";
        char tmp[96];
        char *p = tmp;
        size_t i;
        static const struct { uint32_t bit; const char *text; } names[] = {
            { MBEDTLS_X509_BADCERT_EXPIRED,      "expired"      },
            { MBEDTLS_X509_BADCERT_FUTURE,       "not yet valid (check the clock)" },
            { MBEDTLS_X509_BADCERT_CN_MISMATCH,  "wrong host name" },
            { MBEDTLS_X509_BADCERT_NOT_TRUSTED,  "untrusted issuer" },
            { MBEDTLS_X509_BADCERT_REVOKED,      "revoked"      },
            { MBEDTLS_X509_BADCERT_BAD_MD,       "bad hash"     },
            { MBEDTLS_X509_BADCERT_BAD_PK,       "bad key type" },
            { MBEDTLS_X509_BADCERT_BAD_KEY,      "bad key"      },
        };

        tmp[0] = 0;
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (last_flags & names[i].bit) {
                size_t l = strlen(names[i].text);

                if ((size_t)(p - tmp) + l + 3 < sizeof(tmp)) {
                    if (p != tmp) {
                        *p++ = ',';
                        *p++ = ' ';
                    }
                    memcpy(p, names[i].text, l);
                    p += l;
                    *p = 0;
                }
            }
        }

        if (n + strlen(sep) + 14 + strlen(tmp) < sizeof(errbuf))
            snprintf(errbuf + n, sizeof(errbuf) - n, "%scertificate: %s",
                     sep, tmp[0] ? tmp : "invalid");
    }

    return errbuf;
}

static void session_free(void)
{
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    active = 0;
}

int tls_connect(const char *host, const u8 ip[4], u16 port)
{
    int r;

    if (active)
        return TCP_ERR_BUSY;

    last_code = 0;
    last_flags = 0;
    ver_str = "";
    suite_str = "";

    if (tls_init() != 0)
        return TLS_ERR_INIT;

    if (verify_mode == TLS_VERIFY_REQUIRED && load_roots() != 0) {
        last_code = MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT;
        return TLS_ERR_INIT;
    }

    r = tcp_connect(ip, port, 1000);
    if (r != TCP_OK)
        return r;

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    r = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0)
        goto fail_init;

    mbedtls_ssl_conf_rng(&conf, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
    mbedtls_ssl_conf_read_timeout(&conf, TLS_IDLE_MS);

    if (verify_mode == TLS_VERIFY_REQUIRED) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    r = mbedtls_ssl_setup(&ssl, &conf);
    if (r != 0)
        goto fail_init;

    r = mbedtls_ssl_set_hostname(&ssl, host);
    if (r != 0)
        goto fail_init;

    mbedtls_ssl_set_bio(&ssl, NULL, bio_send, NULL, bio_recv_timeout);

    do {
        r = mbedtls_ssl_handshake(&ssl);
    } while (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (r != 0) {
        last_code = r;
        last_flags = mbedtls_ssl_get_verify_result(&ssl);
        if (last_flags == 0xFFFFFFFFu)
            last_flags = 0;             /* no verification was done */
        session_free();
        tcp_close();
        if (r == MBEDTLS_ERR_SSL_TIMEOUT)
            return TCP_ERR_TIMEOUT;
        return (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || last_flags)
                   ? TLS_ERR_CERT : TLS_ERR_HANDSHAKE;
    }

    ver_str = mbedtls_ssl_get_version(&ssl);
    suite_str = mbedtls_ssl_get_ciphersuite(&ssl);
    active = 1;
    return TCP_OK;

fail_init:
    last_code = r;
    session_free();
    tcp_close();
    return TLS_ERR_INIT;
}

int tls_send(const void *data, u32 length)
{
    const unsigned char *p = (const unsigned char *)data;

    if (!active)
        return TCP_ERR_NET;

    while (length) {
        int r = mbedtls_ssl_write(&ssl, p, length);

        if (r > 0) {
            p += r;
            length -= (u32)r;
        } else if (r == MBEDTLS_ERR_SSL_WANT_READ ||
                   r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        } else {
            last_code = r;
            return r == MBEDTLS_ERR_NET_CONN_RESET ? TCP_ERR_RESET : TLS_ERR_IO;
        }
    }

    return TCP_OK;
}

int tls_recv(void *buf, u16 capacity, u32 timeout_ms)
{
    if (!active)
        return TCP_ERR_NET;

    if (timeout_ms == 0)
        timeout_ms = 1;
    mbedtls_ssl_conf_read_timeout(&conf, timeout_ms);

    for (;;) {
        int r = mbedtls_ssl_read(&ssl, (unsigned char *)buf, capacity);

        if (r > 0)
            return r;
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            return 0;                       /* clean end of stream */
        if (r == MBEDTLS_ERR_SSL_CONN_EOF)
            return 0;                       /* peer closed without close_notify */
        if (r == MBEDTLS_ERR_SSL_TIMEOUT)
            return TCP_ERR_TIMEOUT;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
            r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            continue;                       /* TLS 1.3 post-handshake message */

        last_code = r;
        return r == MBEDTLS_ERR_NET_CONN_RESET ? TCP_ERR_RESET : TLS_ERR_IO;
    }
}

void tls_close(void)
{
    if (active) {
        mbedtls_ssl_close_notify(&ssl);
        session_free();
    }
    tcp_close();
}
