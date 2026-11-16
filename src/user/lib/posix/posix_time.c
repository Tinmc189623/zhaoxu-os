/* Zhaoxu OS - newlib time glue.
 *
 * ZOS does not expose a wall clock to libc yet; gettimeofday and times report
 * a fixed zero so callers that only need a monotonic fallback keep working. */
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Report the epoch until a real clock syscall is wired up. */
int gettimeofday(struct timeval *time, void *timezone)
{
    (void)timezone;
    if (time) {
        time->tv_sec = 0;
        time->tv_usec = 0;
    }
    return 0;
}

/* Process times are not accounted yet. */
clock_t _times(void *buffer)
{
    (void)buffer;
    errno = ENOSYS;
    return (clock_t)-1;
}
