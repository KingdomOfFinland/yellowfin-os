/*
 * tls_glue.c - what Mbed TLS needs from the machine: heap, entropy, clock
 * YellowfinOS
 *
 * Compiled with -DYF_HOST_TEST for the PC-side test harness, where the
 * hardware parts are replaced by host equivalents.
 */

#include <stddef.h>
#include <stdint.h>
#include "time.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/entropy.h"
#include "tls.h"

#ifndef YF_HOST_TEST
#include "ethernet.h"       /* net_now_ms() */
#endif

/* ---- heap ----------------------------------------------------------------
 * Mbed TLS runs from one fixed region. It is NOT in .bss on purpose: .bss
 * already holds the back buffer and sits below the wallpaper cache (0x800000),
 * so a big array there could collide with it.
 *
 *   0x00100000  kernel image (text, data, bss)
 *   0x00800000  wallpaper cache
 *   0x01000000  TLS heap, 2 MiB  <- here
 */
#define YF_TLS_HEAP_BASE  0x01000000u
#define YF_TLS_HEAP_SIZE  0x00200000u

#ifdef YF_HOST_TEST
static unsigned char host_heap[YF_TLS_HEAP_SIZE] __attribute__((aligned(16)));
void *yf_tls_heap(size_t *size)
{
    *size = sizeof(host_heap);
    return host_heap;
}
#else
void *yf_tls_heap(size_t *size)
{
    *size = YF_TLS_HEAP_SIZE;
    return (void *)YF_TLS_HEAP_BASE;
}
#endif

/* ---- port I/O and cycle counter --------------------------------------- */

static inline uint32_t rdtsc_lo(void)
{
    uint32_t lo, hi;

    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return lo;
}

#ifndef YF_HOST_TEST
static inline uint8_t inb(uint16_t p)
{
    uint8_t v;

    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

static inline void outb(uint16_t p, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
#else
static inline uint8_t inb(uint16_t p) { (void)p; return (uint8_t)rdtsc_lo(); }
#endif

/* ---- entropy ------------------------------------------------------------
 * mbedtls_hardware_poll() is the only random source Mbed TLS gets. Samples:
 *   - the time (TSC cycles) that slow ISA-bus port reads take, which jitters
 *     with bus and memory contention,
 *   - whatever other code feeds in through yf_entropy_add().
 * Mbed TLS hashes and conditions the output (SHA-256 into a CTR_DRBG).
 *
 * This is best effort, and weaker in an emulator than on real hardware.
 * Feed it more: call yf_entropy_add() with timer values from key presses,
 * mouse packets, network packet arrivals and disk completions.
 */
static volatile uint32_t pool[4];
static volatile uint32_t pool_idx;

static uint32_t rotl32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

void yf_entropy_add(u32 v)
{
    uint32_t i = pool_idx++ & 3u;

    pool[i] = rotl32(pool[i] ^ v ^ rdtsc_lo(), 7) * 0x9E3779B1u + 0x7F4A7C15u;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                          size_t *olen)
{
    size_t i;
    uint32_t acc = rdtsc_lo();

    (void)data;

    for (i = 0; i < len; i++) {
        int r;

        for (r = 0; r < 24; r++) {
            uint32_t t0 = rdtsc_lo();
            uint8_t a = inb(0x61);
#ifndef YF_HOST_TEST
            uint8_t b = inb(0x40);          /* PIT channel 0 count */
#else
            uint8_t b = 0;
#endif
            uint32_t t1 = rdtsc_lo();

            acc = rotl32(acc, 5) ^ (t1 - t0) ^ ((uint32_t)a << 8) ^ b;
            acc += pool[r & 3];
            acc ^= acc >> 15;
            acc *= 0x2C1B3C6Du;
        }
        yf_entropy_add(acc);
        output[i] = (unsigned char)(acc ^ (acc >> 8) ^ (acc >> 16) ^ (acc >> 24));
    }

    *olen = len;
    return 0;
}

/* ---- calendar math (proleptic Gregorian, Howard Hinnant's algorithms) --- */

static long days_from_civil(long y, unsigned m, unsigned d)
{
    long era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    long long t = (long long)*tt;
    long days, z, era;
    unsigned doe, yoe, doy, mp, d, m;
    long y;
    long secs;

    if (t < 0)
        return NULL;

    days = (long)(t / 86400);
    secs = (long)(t - (long long)days * 86400);

    z = days + 719468L;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = (unsigned)(z - era * 146097L);
    yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    y = (long)yoe + era * 400L;
    doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    mp = (5u * doy + 2u) / 153u;
    d = doy - (153u * mp + 2u) / 5u + 1u;
    m = mp < 10u ? mp + 3u : mp - 9u;
    y += m <= 2;

    tm_buf->tm_sec = (int)(secs % 60);
    tm_buf->tm_min = (int)((secs / 60) % 60);
    tm_buf->tm_hour = (int)(secs / 3600);
    tm_buf->tm_mday = (int)d;
    tm_buf->tm_mon = (int)m - 1;
    tm_buf->tm_year = (int)(y - 1900);
    tm_buf->tm_wday = (int)((days + 4) % 7);
    tm_buf->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm_buf->tm_isdst = 0;
    return tm_buf;
}

/* ---- clock ---------------------------------------------------------------
 * Wall-clock time comes from the CMOS RTC, treated as UTC like the taskbar
 * clock does. time_t is 64-bit, so there is no year-2038 problem here.
 */
#ifndef YF_HOST_TEST

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

struct rtc_raw { uint8_t s, mi, h, d, mo, y, regb; };

static void rtc_snapshot(struct rtc_raw *r)
{
    uint32_t guard = 0;

    while ((cmos_read(0x0A) & 0x80) && ++guard < 1000000u)
        ;                                   /* update in progress */

    r->s = cmos_read(0x00);
    r->mi = cmos_read(0x02);
    r->h = cmos_read(0x04);
    r->d = cmos_read(0x07);
    r->mo = cmos_read(0x08);
    r->y = cmos_read(0x09);
    r->regb = cmos_read(0x0B);
}

static unsigned bcd2bin(uint8_t v)
{
    return (unsigned)((v & 0x0F) + (v >> 4) * 10);
}

time_t time(time_t *out)
{
    struct rtc_raw a, b;
    unsigned s, mi, h, d, mo, y;
    int tries = 0;
    time_t t;

    do {
        rtc_snapshot(&a);
        rtc_snapshot(&b);
    } while ((a.s != b.s || a.mi != b.mi || a.h != b.h || a.d != b.d ||
              a.mo != b.mo || a.y != b.y) && ++tries < 5);

    if (!(b.regb & 0x04)) {                 /* BCD */
        int pm = (b.h & 0x80) != 0;

        s = bcd2bin(b.s);
        mi = bcd2bin(b.mi);
        h = bcd2bin((uint8_t)(b.h & 0x7F));
        d = bcd2bin(b.d);
        mo = bcd2bin(b.mo);
        y = bcd2bin(b.y);
        if (!(b.regb & 0x02) && pm)         /* 12-hour clock, PM */
            h = (h % 12) + 12;
    } else {
        int pm = (b.h & 0x80) != 0;

        s = b.s;
        mi = b.mi;
        h = b.h & 0x7F;
        d = b.d;
        mo = b.mo;
        y = b.y;
        if (!(b.regb & 0x02) && pm)
            h = (h % 12) + 12;
    }

    t = (time_t)days_from_civil(2000 + (long)y, mo, d) * 86400
        + (time_t)h * 3600 + (time_t)mi * 60 + (time_t)s;

    if (out)
        *out = t;
    return t;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    static uint32_t last;
    static uint32_t wraps;
    uint32_t now = (uint32_t)net_now_ms();

    if (now < last)
        wraps++;
    last = now;
    return (mbedtls_ms_time_t)(((uint64_t)wraps << 32) | now);
}

#endif /* !YF_HOST_TEST */
