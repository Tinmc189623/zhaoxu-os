#ifndef ZHAOXU_EVENTFD_H
#define ZHAOXU_EVENTFD_H

#include <types.h>

/* eventfd object used by the Wayland event loop.  The counter is read as a
 * single 64-bit integer; EFD_SEMAPHORE makes each read consume one instead of
 * the whole counter. */
struct eventfd;

struct eventfd *eventfd_create(u32 initial, int semaphore);
void eventfd_ref(struct eventfd *event);
void eventfd_release(struct eventfd *event);
int eventfd_read(struct eventfd *event, int nonblock, u64 *value);
int eventfd_write(struct eventfd *event, u64 value, int nonblock);
short eventfd_poll(struct eventfd *event, short requested);

#endif
