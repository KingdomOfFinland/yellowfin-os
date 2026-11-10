#include "klog.h"
#include "timer.h"
#include <stdarg.h>

#define LOG_SIZE 16384
#define COM1     0x3F8

static char     logbuf[LOG_SIZE];
static uint32_t loglen = 0;
static void   (*sink)(const char *, int) = 0;

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* ---------- serial (COM1, 115200 8N1) ---------- */
static void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_puts(const char *s)
{
    for (; *s; s++) {
        int i;
        for (i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) { }
        if (*s == '\n') outb(COM1, '\r');
        outb(COM1, *s);
    }
}

/* ---------- tiny printf: %c %s %d %u %x %% with 0 and width ---------- */
struct out { char *buf; uint32_t cap, n; };

static void emit(struct out *o, char c)
{
    if (o->n + 1 < o->cap) o->buf[o->n] = c;
    o->n++;
}

static void emit_num(struct out *o, uint32_t v, uint32_t base, int width, char pad, int neg)
{
    char tmp[12];
    int n = 0, len;

    do { tmp[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    len = n + neg;

    if (neg && pad == '0') emit(o, '-');
    while (len < width) { emit(o, pad); len++; }
    if (neg && pad != '0') emit(o, '-');
    while (n) emit(o, tmp[--n]);
}

static void kvfmt(struct out *o, const char *f, va_list ap)
{
    for (; *f; f++) {
        char pad = ' ';
        int width = 0;

        if (*f != '%') { emit(o, *f); continue; }
        f++;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');

        switch (*f) {
        case 'c': emit(o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int l = 0;
            if (!s) s = "(null)";
            while (s[l]) l++;
            while (l < width) { emit(o, ' '); l++; }
            while (*s) emit(o, *s++);
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) emit_num(o, (uint32_t)(-(v + 1)) + 1u, 10, width, pad, 1);
            else       emit_num(o, (uint32_t)v, 10, width, pad, 0);
            break;
        }
        case 'u': emit_num(o, va_arg(ap, uint32_t), 10, width, pad, 0); break;
        case 'x': emit_num(o, va_arg(ap, uint32_t), 16, width, pad, 0); break;
        case '%': emit(o, '%'); break;
        case 0:   return;
        default:  emit(o, '%'); emit(o, *f); break;
        }
    }
}

static void kvsnprintf(char *buf, uint32_t cap, const char *fmt, va_list ap)
{
    struct out o;
    o.buf = buf; o.cap = cap; o.n = 0;
    kvfmt(&o, fmt, ap);
    if (cap) buf[o.n < cap ? o.n : cap - 1] = 0;
}

static void ksnprintf(char *buf, uint32_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, cap, fmt, ap);
    va_end(ap);
}

/* ---------- log buffer ---------- */
static void store(const char *s)
{
    uint32_t n = 0, i;
    while (s[n]) n++;

    if (loglen + n > LOG_SIZE) {                 /* full: drop the oldest half */
        uint32_t half = LOG_SIZE / 2;
        for (i = 0; i < loglen - half; i++) logbuf[i] = logbuf[i + half];
        loglen -= half;
    }
    for (i = 0; i < n; i++) logbuf[loglen++] = s[i];
}

void klog_init(void)
{
    loglen = 0;
    serial_init();
}

void klog_set_sink(void (*fn)(const char *, int)) { sink = fn; }

const char *klog_buffer(uint32_t *len) { *len = loglen; return logbuf; }

const char *klog_tail(int lines, uint32_t *len)
{
    uint32_t i = loglen;

    if (i && logbuf[i - 1] == '\n') i--;          /* ignore the last newline */
    while (i > 0) {
        if (logbuf[i - 1] == '\n' && --lines == 0) break;
        i--;
    }
    *len = loglen - i;
    return logbuf + i;
}

static const char *const lvl_name[] = { "DBG ", "INFO", "WARN", "ERR ", " OK " };

void klog(int level, const char *sub, const char *fmt, ...)
{
    char msg[160], line[220];
    va_list ap;
    uint32_t t = timer_ticks();

    if (level < 0 || level > 4) level = KLOG_INFO;

    va_start(ap, fmt);
    kvsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    ksnprintf(line, sizeof line, "[%5u.%02u] %s %s: %s\n",
              t / 100, t % 100, lvl_name[level], sub, msg);

    store(line);
    serial_puts(line);
    if (sink) sink(line, level);
}

/* ---------- CPU probe ---------- */
static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

void klog_probe_cpu(void)
{
    uint32_t a, b, c, d, maxleaf, maxext;
    char vendor[13], brand[49], feat[64];
    int i, f = 0;

    cpuid(0, &maxleaf, &b, &c, &d);
    for (i = 0; i < 4; i++) {
        vendor[i]     = (char)(b >> (8 * i));
        vendor[4 + i] = (char)(d >> (8 * i));
        vendor[8 + i] = (char)(c >> (8 * i));
    }
    vendor[12] = 0;
    kinfo("cpu", "vendor %s, max cpuid leaf %u", vendor, maxleaf);

    cpuid(0x80000000u, &maxext, &b, &c, &d);
    if (maxext >= 0x80000004u) {
        uint32_t leaf;
        const char *p;
        for (leaf = 0; leaf < 3; leaf++) {
            cpuid(0x80000002u + leaf, &a, &b, &c, &d);
            for (i = 0; i < 4; i++) {
                brand[leaf * 16 + i]      = (char)(a >> (8 * i));
                brand[leaf * 16 + 4 + i]  = (char)(b >> (8 * i));
                brand[leaf * 16 + 8 + i]  = (char)(c >> (8 * i));
                brand[leaf * 16 + 12 + i] = (char)(d >> (8 * i));
            }
        }
        brand[48] = 0;
        p = brand;
        while (*p == ' ') p++;
        kinfo("cpu", "%s", p);
    }

    cpuid(1, &a, &b, &c, &d);
    kinfo("cpu", "family %u model %u stepping %u", (a >> 8) & 0xF, (a >> 4) & 0xF, a & 0xF);

    feat[0] = 0;
    {
        static const struct { int bit; const char *name; } fl[] = {
            {0,"fpu"},{4,"tsc"},{5,"msr"},{6,"pae"},{9,"apic"},{23,"mmx"},{25,"sse"},{26,"sse2"}
        };
        for (i = 0; i < 8; i++) {
            if (d & (1u << fl[i].bit)) {
                const char *s = fl[i].name;
                if (f) feat[f++] = ' ';
                while (*s) feat[f++] = *s++;
            }
        }
        feat[f] = 0;
    }
    kinfo("cpu", "features: %s", feat);
}

/* ---------- PCI probe ---------- */
static uint32_t pci_cfg(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off)
{
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                            | ((uint32_t)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

/* ---------- PCI BAR diagnostic (read-only) ---------- */
static void klog_probe_bars(uint8_t bus, uint8_t slot, uint8_t fn)
{
    uint32_t id;
    uint32_t hdr;
    uint32_t bar;
    uint32_t type;
    uint32_t base;
    uint32_t upper;
    int i;

    id = pci_cfg(bus, slot, fn, 0x00);

    if ((id & 0xFFFFu) == 0xFFFFu)
        return;

    hdr = pci_cfg(bus, slot, fn, 0x0C);

    /* Only inspect normal PCI device headers (type 0). */
    if (((hdr >> 16) & 0x7Fu) != 0)
        return;

    kinfo("pci", "%02x:%02x.%u BAR registers:",
          bus, slot, fn);

    for (i = 0; i < 6; i++) {
        bar = pci_cfg(bus, slot, fn,
                      (uint8_t)(0x10 + i * 4));

        if (bar == 0 || bar == 0xFFFFFFFFu)
            continue;

        if (bar & 1u) {
            /* I/O-space BAR */
            kinfo("pci", "  BAR%d I/O base=%x",
                  i, bar & ~3u);
        } else {
            /* Memory-space BAR */
            type = (bar >> 1) & 3u;
            base = bar & ~0xFu;

            kinfo("pci",
                  "  BAR%d MMIO base=%x type=%u prefetch=%u",
                  i, base, type, (bar >> 3) & 1u);

            if (type == 2u && i < 5) {
                /* 64-bit BAR: next register holds upper 32 bits. */
                upper = pci_cfg(bus, slot, fn,
                                (uint8_t)(0x14 + i * 4));

                kinfo("pci", "  BAR%d upper=%x", i + 1, upper);
                i++;
            }
        }
    }
}

static const struct { uint16_t v, d; const char *name; } known[] = {
    {0x8086, 0x1237, "i440FX host bridge"},
    {0x8086, 0x7000, "PIIX3 ISA bridge"},
    {0x8086, 0x7010, "PIIX3 IDE"},
    {0x8086, 0x7113, "PIIX4 ACPI"},
    {0x8086, 0x100E, "82540EM e1000"},
    {0x1234, 0x1111, "QEMU std VGA"},
};

void klog_probe_pci(void)
{
    int bus, slot, fn, count = 0;

    for (bus = 0; bus < 16; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint32_t id = pci_cfg(bus, slot, 0, 0);
            int nfn;
            if ((id & 0xFFFF) == 0xFFFF) continue;
            nfn = ((pci_cfg(bus, slot, 0, 0x0C) >> 16) & 0x80) ? 8 : 1;

            for (fn = 0; fn < nfn; fn++) {
                uint32_t cls, irq;
                uint16_t ven, dev;
                const char *name = "";
                int k;

                id = pci_cfg(bus, slot, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                ven = (uint16_t)(id & 0xFFFF);
                dev = (uint16_t)(id >> 16);
                cls = pci_cfg(bus, slot, fn, 0x08) >> 16;      /* class:subclass */
                irq = pci_cfg(bus, slot, fn, 0x3C) & 0xFF;

                for (k = 0; k < 6; k++)
                    if (known[k].v == ven && known[k].d == dev) name = known[k].name;

                kinfo("pci", "%02x:%02x.%u %04x:%04x cls %04x irq %u %s",
      bus, slot, fn, ven, dev, cls, irq, name);

klog_probe_bars((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);

count++;
            }
        }
    }
    kinfo("pci", "%u devices found", count);
}