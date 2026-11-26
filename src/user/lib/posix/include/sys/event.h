/* kqueue surface (FreeBSD subsystem); declarations until the BSD event
 * layer is reachable from Ring3. */
#ifndef ZHAOXU_SYS_EVENT_H
#define ZHAOXU_SYS_EVENT_H

#include <sys/types.h>

struct kevent {
    uintptr_t ident;
    short filter;
    unsigned short flags;
    unsigned int fflags;
    intptr_t data;
    void *udata;
};

#define EVFILT_READ (-1)
#define EVFILT_WRITE (-2)
#define EVFILT_AIO (-3)
#define EVFILT_VNODE (-4)
#define EVFILT_PROC (-5)
#define EVFILT_SIGNAL (-6)
#define EVFILT_TIMER (-7)

#define EV_ADD 0x0001
#define EV_DELETE 0x0002
#define EV_ENABLE 0x0004
#define EV_DISABLE 0x0008
#define EV_ONESHOT 0x0010
#define EV_CLEAR 0x0020
#define EV_RECEIPT 0x0040

#define EV_EOF 0x8000
#define EV_ERROR 0x4000

#define NOTE_DELETE 0x0001
#define NOTE_WRITE 0x0002
#define NOTE_EXTEND 0x0004
#define NOTE_ATTRIB 0x0008
#define NOTE_LINK 0x0010
#define NOTE_RENAME 0x0020
#define NOTE_REVOKE 0x0040

#define EV_SET(kevp, a, b, c, d, e, f) do { \
    (kevp)->ident = (a); \
    (kevp)->filter = (b); \
    (kevp)->flags = (c); \
    (kevp)->fflags = (d); \
    (kevp)->data = (e); \
    (kevp)->udata = (f); \
} while (0)

int kqueue(void);
int kevent(int fd, const struct kevent *changes, int change_count,
           struct kevent *events, int event_count,
           const struct timespec *timeout);

#endif
