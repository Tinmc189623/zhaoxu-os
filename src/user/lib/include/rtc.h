#ifndef ZHAOXU_USER_RTC_H
#define ZHAOXU_USER_RTC_H

#include <types.h>

struct rtc_time {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u16 year;
    u8 weekday;
};

int rtc_read(struct rtc_time *time);

#endif
