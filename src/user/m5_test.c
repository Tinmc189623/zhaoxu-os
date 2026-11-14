/*
 * Zhaoxu OS - M5 event loop self-test.
 *
 * Exercises the kernel primitives stock libwayland needs before the library
 * port exists: eventfd counters, timerfd expirations and epoll readiness.
 * Run it from 8sh with `/data/M5_TEST.ZXE`.
 */
#include "userlib.h"

static int fail(const char *where)
{
    u_puts("[m5] FAIL: ");
    u_puts(where);
    u_puts("\n");
    return 1;
}

int user_main(long *stack)
{
    (void)stack;

    int event = (int)sys_eventfd(0, EFD_NONBLOCK);
    if (event < 0)
        return fail("eventfd");

    u64 value = 9;
    if (sys_write(event, &value, sizeof(value)) != (long)sizeof(value))
        return fail("eventfd write");

    struct pollfd ready = { event, POLLIN, 0 };
    if (sys_poll(&ready, 1, 0) != 1 || !(ready.revents & POLLIN))
        return fail("eventfd poll");

    u64 result = 0;
    if (sys_read(event, &result, sizeof(result)) != (long)sizeof(result) ||
        result != 9)
        return fail("eventfd read");
    if (sys_read(event, &result, sizeof(result)) != -1)
        return fail("eventfd nonblock");
    u_puts("[m5] eventfd PASS\n");

    int epoll = (int)sys_epoll_create1(0);
    if (epoll < 0)
        return fail("epoll_create1");

    struct zos_epoll_event registration;
    registration.events = EPOLLIN;
    registration.data = 0x1234;
    if (sys_epoll_ctl(epoll, EPOLL_CTL_ADD, event, &registration) != 0)
        return fail("epoll_ctl add");

    value = 3;
    if (sys_write(event, &value, sizeof(value)) != (long)sizeof(value))
        return fail("epoll eventfd write");

    struct zos_epoll_event events[4];
    long count = sys_epoll_wait(epoll, events, 4, 0);
    if (count != 1 || events[0].data != 0x1234 ||
        !(events[0].events & EPOLLIN))
        return fail("epoll_wait eventfd");
    sys_read(event, &result, sizeof(result));
    u_puts("[m5] epoll PASS\n");

    int timer = (int)sys_timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer < 0)
        return fail("timerfd_create");

    struct zos_itimerspec spec;
    spec.interval.seconds = 0;
    spec.interval.nanoseconds = 0;
    spec.value.seconds = 0;
    spec.value.nanoseconds = 50 * 1000 * 1000;
    if (sys_timerfd_settime(timer, 0, &spec, 0) != 0)
        return fail("timerfd_settime");

    struct pollfd expired = { timer, POLLIN, 0 };
    if (sys_poll(&expired, 1, 500) != 1 || !(expired.revents & POLLIN))
        return fail("timerfd poll");

    u64 expirations = 0;
    if (sys_read(timer, &expirations, sizeof(expirations)) !=
            (long)sizeof(expirations) ||
        expirations != 1)
        return fail("timerfd read");
    u_puts("[m5] timerfd PASS\n");

    struct zos_epoll_event timer_registration;
    timer_registration.events = EPOLLIN;
    timer_registration.data = 0x5678;
    if (sys_epoll_ctl(epoll, EPOLL_CTL_ADD, timer, &timer_registration) != 0)
        return fail("epoll_ctl timer");

    spec.value.nanoseconds = 20 * 1000 * 1000;
    if (sys_timerfd_settime(timer, 0, &spec, 0) != 0)
        return fail("timerfd rearm");

    count = sys_epoll_wait(epoll, events, 4, 500);
    if (count != 1 || events[0].data != 0x5678)
        return fail("epoll_wait timer");
    sys_read(timer, &expirations, sizeof(expirations));

    count = sys_epoll_wait(epoll, events, 4, 10);
    if (count != 0)
        return fail("epoll_wait timeout");

    u_puts("[m5] eventfd, timerfd, epoll PASS\n");
    return 0;
}
