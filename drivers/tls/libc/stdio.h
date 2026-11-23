#ifndef YF_LIBC_STDIO_H
#define YF_LIBC_STDIO_H
#include <stddef.h>
#include <stdarg.h>
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int printf(const char *fmt, ...);
#endif
