#include "rtc.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

#define RTC_UIP      0x80
#define RTC_BINARY   0x04
#define RTC_24_HOUR  0x02
#define RTC_PM       0x80

static unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static unsigned char rtc_read_register(unsigned char reg)
{
    /* Keep NMI enabled while selecting the CMOS register. */
    outb(CMOS_ADDRESS, reg & 0x7F);
    return inb(CMOS_DATA);
}

static int rtc_wait_ready(void)
{
    unsigned int timeout = 1000000;

    while (timeout--) {
        if (!(rtc_read_register(RTC_STATUS_A) & RTC_UIP))
            return 1;
    }

    return 0;
}

static unsigned char from_bcd(unsigned char value)
{
    return (unsigned char)(
        (value & 0x0F) + ((value >> 4) * 10)
    );
}

struct rtc_raw {
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned char year;
    unsigned char status_b;
};

static int rtc_read_raw(struct rtc_raw *raw)
{
    if (!rtc_wait_ready())
        return 0;

    raw->second  = rtc_read_register(RTC_SECONDS);
    raw->minute  = rtc_read_register(RTC_MINUTES);
    raw->hour    = rtc_read_register(RTC_HOURS);
    raw->day     = rtc_read_register(RTC_DAY);
    raw->month   = rtc_read_register(RTC_MONTH);
    raw->year    = rtc_read_register(RTC_YEAR);
    raw->status_b = rtc_read_register(RTC_STATUS_B);

    return 1;
}

static int rtc_raw_equal(
    const struct rtc_raw *a,
    const struct rtc_raw *b
)
{
    return a->second == b->second &&
           a->minute == b->minute &&
           a->hour == b->hour &&
           a->day == b->day &&
           a->month == b->month &&
           a->year == b->year &&
           a->status_b == b->status_b;
}

int rtc_get_datetime(struct rtc_datetime *dt)
{
    struct rtc_raw a;
    struct rtc_raw b;
    unsigned int attempts;

    if (dt == 0)
        return 0;

    /* Read twice to avoid accepting a value across a clock update. */
    for (attempts = 0; attempts < 10; attempts++) {
        if (!rtc_read_raw(&a))
            return 0;

        if (!rtc_read_raw(&b))
            return 0;

        if (rtc_raw_equal(&a, &b))
            break;
    }

    if (attempts == 10)
        return 0;

    {
        unsigned char hour = b.hour;
        int is_binary = (b.status_b & RTC_BINARY) != 0;
        int is_24_hour = (b.status_b & RTC_24_HOUR) != 0;
        int is_pm = (hour & RTC_PM) != 0;

        /* In 12-hour mode, bit 7 indicates PM. */
        if (!is_24_hour)
            hour &= 0x7F;

        if (!is_binary) {
            b.second = from_bcd(b.second);
            b.minute = from_bcd(b.minute);
            hour     = from_bcd(hour);
            b.day    = from_bcd(b.day);
            b.month  = from_bcd(b.month);
            b.year   = from_bcd(b.year);
        }

        if (!is_24_hour) {
            hour %= 12;

            if (is_pm)
                hour += 12;
        }

        /* Basic sanity checks against invalid RTC values. */
        if (b.second > 59 || b.minute > 59 || hour > 23 ||
            b.day < 1 || b.day > 31 ||
            b.month < 1 || b.month > 12 ||
            b.year > 99)
            return 0;

        dt->second = b.second;
        dt->minute = b.minute;
        dt->hour   = hour;
        dt->day    = b.day;
        dt->month  = b.month;

        /* Assumes the RTC year is in the range 2000–2099. */
        dt->year = 2000 + b.year;
    }

    return 1;
}