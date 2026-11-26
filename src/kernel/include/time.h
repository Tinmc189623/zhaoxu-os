#ifndef ZHAOXU_TIME_H
#define ZHAOXU_TIME_H

#include <rtc.h>
#include <types.h>

/* Convert a wall-clock RTC reading to seconds since 1970-01-01T00:00:00Z. */
i64 time_epoch_seconds(const struct rtc_time *clock);

#endif
