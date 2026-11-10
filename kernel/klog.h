#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>

#define KLOG_DEBUG 0
#define KLOG_INFO  1
#define KLOG_WARN  2
#define KLOG_ERR   3
#define KLOG_OK    4

void        klog_init(void);                                   /* serial + buffer */
void        klog_set_sink(void (*fn)(const char *line, int level));  /* screen output */
void        klog(int level, const char *sub, const char *fmt, ...);
const char *klog_buffer(uint32_t *len);
const char *klog_tail(int lines, uint32_t *len);
void        klog_probe_cpu(void);
void        klog_probe_pci(void);

#define kdebug(sub, ...) klog(KLOG_DEBUG, sub, __VA_ARGS__)
#define kinfo(sub, ...)  klog(KLOG_INFO,  sub, __VA_ARGS__)
#define kwarn(sub, ...)  klog(KLOG_WARN,  sub, __VA_ARGS__)
#define kerr(sub, ...)   klog(KLOG_ERR,   sub, __VA_ARGS__)
#define kok(sub, ...)    klog(KLOG_OK,    sub, __VA_ARGS__)

#endif