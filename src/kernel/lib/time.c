/*
 * Zhaoxu OS - civil time conversion.
 *
 * clock_gettime(CLOCK_REALTIME) reads the RTC, which the firmware leaves in
 * UTC; this file turns that calendar value into a Unix timestamp.  The
 * monotonic clock is timer_now_ns() and never passes through here.
 */
#include <rtc.h>
#include <time.h>
#include <types.h>

/* Days from 1970-01-01 for a proleptic Gregorian date (Howard Hinnant). */
static i64 days_from_civil(i64 year, u32 month, u32 day)
{
    year -= month <= 2;
    i64 era = (year >= 0 ? year : year - 399) / 400;
    u32 year_of_era = (u32)(year - era * 400);
    u32 day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    u32 day_of_era = year_of_era * 365 + year_of_era / 4 -
                     year_of_era / 100 + day_of_year;
    return era * 146097 + (i64)day_of_era - 719468;
}

i64 time_epoch_seconds(const struct rtc_time *clock)
{
    if (!clock)
        return 0;
    i64 days = days_from_civil(clock->year, clock->month, clock->day);
    return days * 86400 + (i64)clock->hour * 3600 + (i64)clock->minute * 60 +
           clock->second;
}
