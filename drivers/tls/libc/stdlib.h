#ifndef YF_LIBC_STDLIB_H
#define YF_LIBC_STDLIB_H
#include <stddef.h>
void *calloc(size_t n, size_t size);
void  free(void *p);
void  exit(int code) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));
#endif
