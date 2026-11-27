/*
 * Zhaoxu OS - clock reads and sleeps over the kernel's RTC and timer.
 *
 * The RTC syscall returns broken-down local fields; this file converts them
 * to Unix time.  Monotonic time comes from the kernel's millisecond uptime.
 * Nothing can set the clock yet.
 */
#include "posix_glue.h"

#include <errno.h>
#include <stddef.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <time.h>

/* newlib omits the POSIX clock ids on some targets. */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/* Days since 1970-01-01 for a proleptic Gregorian date. */
static long long days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    long long era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (unsigned)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5
                              + day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

/* Read the real-time clock as Unix seconds. */
static int realtime_seconds(long long *seconds)
{
    struct zos_rtc_time now;
    if (zos_syscall3(ZOS_RTC, (long)(uintptr_t)&now, 0, 0) != 0)
        return -1;
    long long days = days_from_civil(now.year, now.month, now.day);
    *seconds = days * 86400 + now.hour * 3600 + now.minute * 60 + now.second;
    return 0;
}

int clock_gettime(int clock, struct timespec *value)
{
    if (!value) {
        errno = EINVAL;
        return -1;
    }
    if (clock == CLOCK_MONOTONIC) {
        long milliseconds = zos_syscall3(ZOS_UPTIME, 0, 0, 0);
        if (milliseconds < 0)
            milliseconds = 0;
        value->tv_sec = milliseconds / 1000;
        value->tv_nsec = (milliseconds % 1000) * 1000000L;
        return 0;
    }
    if (clock == CLOCK_REALTIME) {
        long long seconds;
        if (realtime_seconds(&seconds) != 0 || seconds < 0) {
            errno = EIO;
            return -1;
        }
        value->tv_sec = (time_t)seconds;
        value->tv_nsec = 0;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int clock_settime(int clock, const struct timespec *value)
{
    (void)clock;
    (void)value;
    errno = ENOSYS;
    return -1;
}

int settimeofday(const struct timeval *value, const struct timezone *zone)
{
    (void)value;
    (void)zone;
    errno = ENOSYS;
    return -1;
}

int adjtimex(struct timex *state)
{
    (void)state;
    errno = ENOSYS;
    return -1;
}

/* Sleep for the requested interval, rounded up to the kernel's millisecond. */
int nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (remaining)
        remaining->tv_sec = 0, remaining->tv_nsec = 0;
    if (!request || request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    long long milliseconds =
        (long long)request->tv_sec * 1000 + (request->tv_nsec + 999999) / 1000000;
    if (milliseconds > 0)
        zos_syscall3(ZOS_SLEEP, (long)milliseconds, 0, 0);
    return 0;
}

/* BSD-era short sleep; newlib does not provide it on this target. */
int usleep(useconds_t microseconds)
{
    struct timespec request;
    request.tv_sec = (time_t)(microseconds / 1000000u);
    request.tv_nsec = (long)(microseconds % 1000000u) * 1000L;
    return nanosleep(&request, NULL);
}
