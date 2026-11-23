/*
 * libc.c - the few libc functions Mbed TLS needs
 * YellowfinOS
 *
 * Everything here is weak, so a strong definition elsewhere in the kernel
 * (for example your own memcpy) wins and the linker never reports a
 * duplicate.
 *
 * memcpy, memmove and memset use inline assembly on purpose: GCC turns
 * plain C copy loops into calls to memcpy, which would recurse forever.
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "assert.h"

#define WEAK __attribute__((weak))

WEAK void *memcpy(void *d, const void *s, size_t n)
{
    void *ret = d;

    __asm__ volatile ("cld; rep movsb"
                      : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return ret;
}

WEAK void *memmove(void *d, const void *s, size_t n)
{
    void *ret = d;

    if ((uintptr_t)d <= (uintptr_t)s || (uintptr_t)d >= (uintptr_t)s + n) {
        __asm__ volatile ("cld; rep movsb"
                          : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    } else {
        d = (char *)d + n - 1;
        s = (const char *)s + n - 1;
        __asm__ volatile ("std; rep movsb; cld"
                          : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
    return ret;
}

WEAK void *memset(void *d, int c, size_t n)
{
    void *ret = d;

    __asm__ volatile ("cld; rep stosb"
                      : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return ret;
}

WEAK int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (n--) {
        if (*p != *q)
            return *p - *q;
        p++;
        q++;
    }
    return 0;
}

WEAK void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;

    while (n--) {
        if (*p == (unsigned char)c)
            return (void *)p;
        p++;
    }
    return NULL;
}

WEAK size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;
    return n;
}

WEAK int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

WEAK int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

WEAK char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

WEAK char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);

    if (!nl)
        return (char *)h;
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, nl) == 0)
            return (char *)h;
    return NULL;
}

WEAK char *strcpy(char *d, const char *s)
{
    char *r = d;

    while ((*d++ = *s++))
        ;
    return r;
}

WEAK char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

/* ---- memory: replaced at run time by Mbed TLS's buffer allocator -------- */

WEAK void *calloc(size_t n, size_t size)
{
    (void)n;
    (void)size;
    return NULL;
}

WEAK void free(void *p)
{
    (void)p;
}

WEAK void exit(int code)
{
    (void)code;
    for (;;)
        __asm__ volatile ("cli; hlt");
}

WEAK void abort(void)
{
    exit(1);
}

WEAK void yf_assert_fail(const char *expr, const char *file, int line)
{
    (void)expr;
    (void)file;
    (void)line;
    exit(1);
}

/* ---- snprintf: %s %c %d %i %u %x %X %p %%, flags 0 and width, l/ll ---- */

static void put_num(char *buf, size_t size, size_t *pos, unsigned long long v,
                    int base, int upper, int neg, int width, int zero)
{
    char tmp[24];
    int n = 0, i;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        tmp[n++] = digits[(unsigned)(v % (unsigned)base)];
        v /= (unsigned)base;
    } while (v);

    if (neg && !zero)
        tmp[n++] = '-';
    while (n < width - (neg && zero ? 1 : 0))
        tmp[n++] = zero ? '0' : ' ';
    if (neg && zero)
        tmp[n++] = '-';

    for (i = n - 1; i >= 0; i--) {
        if (*pos + 1 < size)
            buf[*pos] = tmp[i];
        (*pos)++;
    }
}

WEAK int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t pos = 0;

    for (; *fmt; fmt++) {
        int width = 0, zero = 0, lng = 0;

        if (*fmt != '%') {
            if (pos + 1 < size)
                buf[pos] = *fmt;
            pos++;
            continue;
        }

        fmt++;
        if (*fmt == '0') {
            zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);

            if (!s)
                s = "(null)";
            while (*s) {
                if (pos + 1 < size)
                    buf[pos] = *s;
                pos++;
                s++;
            }
            break;
        }
        case 'c':
            if (pos + 1 < size)
                buf[pos] = (char)va_arg(ap, int);
            pos++;
            break;
        case 'd':
        case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long)
                                   : (long long)va_arg(ap, long);
            if (lng == 0)
                v = (int)v;
            put_num(buf, size, &pos,
                    v < 0 ? (unsigned long long)(-v) : (unsigned long long)v,
                    10, 0, v < 0, width, zero);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long)
                                            : (unsigned long long)va_arg(ap, unsigned long);
            if (lng == 0)
                v = (unsigned)v;
            put_num(buf, size, &pos, v, *fmt == 'u' ? 10 : 16,
                    *fmt == 'X', 0, width, zero);
            break;
        }
        case 'p':
            put_num(buf, size, &pos, (unsigned long long)(uintptr_t)va_arg(ap, void *),
                    16, 0, 0, 8, 1);
            break;
        case '%':
            if (pos + 1 < size)
                buf[pos] = '%';
            pos++;
            break;
        default:
            break;
        }
    }

    if (size)
        buf[pos < size ? pos : size - 1] = 0;
    return (int)pos;
}

WEAK int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

WEAK int printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
