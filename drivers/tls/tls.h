#ifndef TLS_H
#define TLS_H

/*
 * tls.h - TLS 1.2 / 1.3 client on top of tcp.c (Mbed TLS inside)
 * YellowfinOS
 *
 * Same shape as the TCP API: connect, send, recv, close. The return codes are
 * the TCP_ERR_* ones plus the TLS_ERR_* ones below.
 */

#include "tcp.h"

#define TLS_ERR_HANDSHAKE  -10      /* handshake failed (see tls_last_error)   */
#define TLS_ERR_CERT       -11      /* certificate did not verify              */
#define TLS_ERR_IO         -12      /* record layer error after the handshake  */
#define TLS_ERR_INIT       -13      /* heap, crypto or CA store setup failed   */

#define TLS_VERIFY_NONE      0      /* accept any certificate (INSECURE)       */
#define TLS_VERIFY_REQUIRED  1      /* chain + host name + dates must be valid */

/* Opens TCP, then does the TLS handshake with SNI and verification as set by
   tls_set_verify(). host is used for SNI and for the certificate name check.
   Returns TCP_OK or a negative error. */
int  tls_connect(const char *host, const u8 ip[4], u16 port);

/* Sends all the data. Returns TCP_OK or a negative error. */
int  tls_send(const void *data, u32 length);

/* Waits up to timeout_ms for decrypted data. Returns bytes copied (>0), 0 at
   end of stream (peer closed), or a negative error. */
int  tls_recv(void *buf, u16 capacity, u32 timeout_ms);

/* Sends close_notify, closes the TCP connection and frees the session. */
void tls_close(void);

void tls_set_verify(int mode);
int  tls_get_verify(void);

/* Adds one more trusted root at run time: PEM text (len includes the final
   NUL) or one DER certificate. Returns 0 or a negative Mbed TLS error. */
int  tls_add_ca(const unsigned char *data, unsigned len);

/* Information about the last (or current) connection. */
const char *tls_version(void);          /* "TLSv1.3", ... or "" */
const char *tls_ciphersuite(void);
int         tls_last_code(void);        /* last Mbed TLS error code, 0 = none */
const char *tls_last_error(void);       /* readable text incl. certificate flags */
unsigned    tls_ca_count(void);         /* trusted roots loaded */

/* Randomness: call with timer values (key presses, mouse packets, packet
   arrivals, disk completions). Cheap and safe from interrupt handlers. */
void yf_entropy_add(u32 v);

#endif
