#ifndef YF_LIBC_ASSERT_H
#define YF_LIBC_ASSERT_H
void yf_assert_fail(const char *expr, const char *file, int line) __attribute__((noreturn));
#define assert(e) ((e) ? (void)0 : yf_assert_fail(#e, __FILE__, __LINE__))
#endif
