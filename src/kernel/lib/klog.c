/*
 * Zhaoxu OS - kernel log ring.
 *
 * console_putc() mirrors every byte here so dmesg can read the boot log
 * after userspace starts.  A fixed 64 KiB ring keeps the kernel allocation
 * free; the spinlock protects against timer-preempted writers.
 */
#include <klog.h>
#include <spinlock.h>
#include <types.h>

static char g_klog[KLOG_BYTES];
static u32 g_head;                 /* next write position */
static u32 g_count;                /* bytes currently stored */
static spinlock_t g_lock = SPINLOCK_INIT;

void klog_putc(char c)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    g_klog[g_head] = c;
    g_head = (g_head + 1) % KLOG_BYTES;
    if (g_count < KLOG_BYTES)
        g_count++;
    spin_unlock_irqrestore(&g_lock, flags);
}

u32 klog_size(void)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    u32 size = g_count;
    spin_unlock_irqrestore(&g_lock, flags);
    return size;
}

/* Copy the oldest-to-newest bytes, limited to the last `length` of them. */
u32 klog_read(char *buffer, u32 length, int clear)
{
    if (!buffer || length == 0)
        return 0;

    u64 flags = spin_lock_irqsave(&g_lock);
    u32 wanted = length < g_count ? length : g_count;
    u32 start = (g_head + KLOG_BYTES - g_count) % KLOG_BYTES;
    start = (start + g_count - wanted) % KLOG_BYTES;
    for (u32 i = 0; i < wanted; i++)
        buffer[i] = g_klog[(start + i) % KLOG_BYTES];
    if (clear)
        g_count = 0;
    spin_unlock_irqrestore(&g_lock, flags);
    return wanted;
}

void klog_clear(void)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    g_count = 0;
    spin_unlock_irqrestore(&g_lock, flags);
}
