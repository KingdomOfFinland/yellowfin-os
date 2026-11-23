/*
 * yf_mbedtls_config.h - Mbed TLS 3.6 configuration for YellowfinOS
 *
 * TLS client only: TLS 1.2 + TLS 1.3, X.509 certificate verification,
 * no filesystem, no sockets, no threads, no libc beyond drivers/tls/libc.
 *
 * mbedtls/include/mbedtls/mbedtls_config.h in this tree just includes this
 * file, so no -DMBEDTLS_CONFIG_FILE is needed (and no quoting trouble in
 * build.bat).
 */
#ifndef YF_MBEDTLS_CONFIG_H
#define YF_MBEDTLS_CONFIG_H

/* ---- system / platform ------------------------------------------------ */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT        /* mbedtls_hardware_poll() in tls_glue.c */
#define MBEDTLS_PLATFORM_TIME_ALT           /* mbedtls_time() in tls_glue.c          */
#define MBEDTLS_PLATFORM_MS_TIME_ALT        /* mbedtls_ms_time() in tls_glue.c       */
#define MBEDTLS_PLATFORM_GMTIME_R_ALT       /* mbedtls_platform_gmtime_r() in tls_glue.c */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE              /* certificate validity dates are checked */
#define MBEDTLS_PLATFORM_MEMORY

/* ---- crypto primitives -------------------------------------------------- */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C                      /* a few Mozilla roots are SHA-1 self-signed; chains stay SHA-256+ */
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_HKDF_C
#define MBEDTLS_HMAC_DRBG_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* ---- public key crypto -------------------------------------------------- */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_PSA_CRYPTO_C                /* TLS 1.3 needs PSA in Mbed TLS 3.6 */

/* ---- X.509 --------------------------------------------------------------- */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_REMOVE_INFO
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT

/* ---- TLS ----------------------------------------------------------------- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_SSL_IN_CONTENT_LEN   16384  /* servers may send full-size records */
#define MBEDTLS_SSL_OUT_CONTENT_LEN   4096  /* we only send small requests        */

/* ---- diagnostics ---------------------------------------------------------- */
#define MBEDTLS_ERROR_C
#define MBEDTLS_VERSION_C

#endif /* YF_MBEDTLS_CONFIG_H */
