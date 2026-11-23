#ifndef YF_LIBC_TIME_H
#define YF_LIBC_TIME_H
/* 64-bit time_t: no year-2038 problem. */
typedef long long time_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};
time_t time(time_t *t);
#endif
