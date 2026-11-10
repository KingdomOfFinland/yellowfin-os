#ifndef RTC_H
#define RTC_H

struct rtc_datetime {
    unsigned int second;
    unsigned int minute;
    unsigned int hour;
    unsigned int day;
    unsigned int month;
    unsigned int year;
};

int rtc_get_datetime(struct rtc_datetime *dt);

#endif