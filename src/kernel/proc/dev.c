/*
 * Zhaoxu OS - kernel side of the Ring3 driver service.
 *
 * The driver process owns every device.  The kernel talks to it through a
 * small set of physical pages that both sides access through the identity
 * map: a header page (requests + disk info), a 4 KiB DMA page, an input page
 * (keyboard ring + mouse state) and a log page drained to the serial port by
 * the driver.
 */
#include <console.h>
#include <dev.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <rtc.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

#define DEV_IO_SECTORS 8
#define DEV_WAIT_MS 5000

static struct dev_header *g_header;
static u8 *g_data;
static struct dev_input *g_input;
static struct dev_log *g_log;
static u64 g_header_phys;
static volatile int g_dev_busy;
static spinlock_t g_log_lock = SPINLOCK_INIT;
static int g_dev_ready;
static int g_info_ready;

static void *phys_ptr(u64 physical)
{
    return (void *)(uintptr_t)physical;
}

void dev_memory_init(void)
{
    u64 header_phys = pmm_alloc_page();
    u64 data_phys = pmm_alloc_page();
    u64 input_phys = pmm_alloc_page();
    u64 log_phys = pmm_alloc_page();
    if (!header_phys || !data_phys || !input_phys || !log_phys)
        return;

    g_header = phys_ptr(header_phys);
    g_data = phys_ptr(data_phys);
    g_input = phys_ptr(input_phys);
    g_log = phys_ptr(log_phys);
    g_header_phys = header_phys;

    memset(g_header, 0, PAGE_SIZE);
    memset(g_data, 0, PAGE_SIZE);
    memset(g_input, 0, PAGE_SIZE);
    memset(g_log, 0, PAGE_SIZE);

    g_header->magic = DEV_MAGIC;
    g_header->state = DEV_STATE_IDLE;
    g_header->data_phys = data_phys;
    g_header->input_phys = input_phys;
    g_header->log_phys = log_phys;
    g_header->width = boot_get_info()->width;
    g_header->height = boot_get_info()->height;
    g_header->rsdp = boot_get_info()->rsdp;
}

void dev_log_write(const char *text, usize length)
{
    if (!g_log || !text || !length)
        return;

    u64 flags = spin_lock_irqsave(&g_log_lock);
    for (usize i = 0; i < length; i++) {
        u32 next = (g_log->head + 1) % DEV_LOG_SIZE;
        if (next == g_log->tail)
            break;
        g_log->data[g_log->head] = text[i];
        g_log->head = next;
    }
    spin_unlock_irqrestore(&g_log_lock, flags);
}

static void dev_copy_to_user(const void *source, u64 length)
{
    if (length > PAGE_SIZE)
        length = PAGE_SIZE;
    memcpy(g_data, source, (usize)length);
}

static void dev_copy_from_user(void *destination, u64 length)
{
    if (length > PAGE_SIZE)
        length = PAGE_SIZE;
    memcpy(destination, g_data, (usize)length);
}

/* g_data is one shared page for every driver request, so payload copies must
 * happen inside the busy window; otherwise a concurrent RTC/disk call from
 * another process can overwrite the bytes between copy and processing. */
static long dev_call_io(u32 op, u32 disk, u64 lba, u32 count,
                        const void *send, u64 send_length, void *recv,
                        u64 recv_length)
{
    long result = -1;
    while (__atomic_test_and_set(&g_dev_busy, __ATOMIC_ACQUIRE))
        thread_sleep_ms(1);

    if (send)
        dev_copy_to_user(send, send_length);

    g_header->op = op;
    g_header->status = 0;
    g_header->disk = disk;
    g_header->count = count;
    g_header->lba = lba;
    __sync_synchronize();
    g_header->state = DEV_STATE_REQUEST;
    __sync_synchronize();

    for (volatile int spin = 0; spin < 2000; spin++) {
        if (g_header->state == DEV_STATE_DONE)
            break;
        cpu_relax();
    }

    u64 waited = 0;
    while (g_header->state != DEV_STATE_DONE) {
        if (waited >= DEV_WAIT_MS) {
            kprintf("[dev ] rpc timeout op %u disk %u lba %llu\n",
                    op, disk, lba);
            g_header->state = DEV_STATE_IDLE;
            __atomic_clear(&g_dev_busy, __ATOMIC_RELEASE);
            return -1;
        }
        thread_sleep_ms(1);
        waited++;
    }

    result = g_header->status;
    if (result == 0 && recv)
        dev_copy_from_user(recv, recv_length);
    g_header->state = DEV_STATE_IDLE;
    __atomic_clear(&g_dev_busy, __ATOMIC_RELEASE);
    return result;
}

static long dev_call(u32 op, u32 disk, u64 lba, u32 count)
{
    return dev_call_io(op, disk, lba, count, NULL, 0, NULL, 0);
}

int dev_start(void)
{
    if (!g_header)
        return -1;

    static char argument[32];
    usize length = 0;
    argument[length++] = '0';
    argument[length++] = 'x';
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        argument[length++] = digits[(g_header_phys >> shift) & 0xF];
    argument[length] = 0;

    char *argv[1] = {argument};
    u64 pid = process_spawn("/bin/drivers", 1, argv);
    if (!pid)
        return -1;

    u64 waited = 0;
    while (g_header->state != DEV_STATE_READY) {
        if (waited >= DEV_WAIT_MS) {
            kprintf("[dev ] driver did not become ready\n");
            return -1;
        }
        thread_sleep_ms(1);
        waited++;
    }
    g_dev_ready = 1;
    return 0;
}

static int dev_info(void)
{
    if (g_info_ready)
        return 0;
    if (dev_call(DEV_OP_INFO, 0, 0, 0) != 0)
        return -1;
    __sync_synchronize();
    g_info_ready = 1;
    return 0;
}

int dev_disk_count(void)
{
    if (dev_info() != 0)
        return 0;
    return (int)g_header->disk_count;
}

u64 dev_disk_sectors(int disk)
{
    if (dev_info() != 0 || disk < 0 || disk >= (int)g_header->disk_count)
        return 0;
    return g_header->sectors[disk];
}

int dev_disk_port(int disk)
{
    if (dev_info() != 0 || disk < 0 || disk >= (int)g_header->disk_count)
        return -1;
    return (int)g_header->ports[disk];
}

const char *dev_disk_model(int disk)
{
    if (dev_info() != 0 || disk < 0 || disk >= (int)g_header->disk_count)
        return "";
    return g_header->models[disk];
}

int dev_disk_read(int disk, u64 lba, u32 count, void *buffer)
{
    u8 *out = buffer;
    while (count) {
        u32 chunk = count > DEV_IO_SECTORS ? DEV_IO_SECTORS : count;
        if (dev_call_io(DEV_OP_READ, (u32)disk, lba, chunk, NULL, 0, out,
                        (u64)chunk * 512) != 0)
            return -1;
        out += (u64)chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

int dev_disk_write(int disk, u64 lba, u32 count, const void *buffer)
{
    const u8 *in = buffer;
    while (count) {
        u32 chunk = count > DEV_IO_SECTORS ? DEV_IO_SECTORS : count;
        if (dev_call_io(DEV_OP_WRITE, (u32)disk, lba, chunk, in,
                        (u64)chunk * 512, NULL, 0) != 0)
            return -1;
        in += (u64)chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

int dev_disk_flush(int disk)
{
    return dev_call(DEV_OP_FLUSH, (u32)disk, 0, 0) == 0 ? 0 : -1;
}

int dev_rtc_read(struct rtc_time *time)
{
    static struct rtc_time cached;
    static u64 cached_tick;
    static int valid;

    u64 now = timer_ticks();
    if (!valid || now - cached_tick >= 500) {
        if (dev_call_io(DEV_OP_RTC, 0, 0, 0, NULL, 0, &cached,
                        sizeof(cached)) != 0)
            return -1;
        cached_tick = now;
        valid = 1;
    }
    *time = cached;
    return 0;
}

int dev_key_pop(void)
{
    if (!g_input)
        return -1;
    u32 head = __atomic_load_n(&g_input->head, __ATOMIC_ACQUIRE);
    u32 tail = __atomic_load_n(&g_input->tail, __ATOMIC_RELAXED);
    if (tail == head)
        return -1;
    char c = g_input->keys[tail];
    __atomic_store_n(&g_input->tail, (tail + 1) % sizeof(g_input->keys),
                     __ATOMIC_RELEASE);
    return (int)(u8)c;
}

int dev_key_has_input(void)
{
    return g_input &&
           __atomic_load_n(&g_input->head, __ATOMIC_ACQUIRE) !=
               __atomic_load_n(&g_input->tail, __ATOMIC_RELAXED);
}

void dev_mouse_state(u64 state[3], int *ready)
{
    if (!g_input) {
        state[0] = state[1] = state[2] = 0;
        if (ready)
            *ready = 0;
        return;
    }
    state[0] = (u64)(i64)g_input->mouse_x;
    state[1] = (u64)(i64)g_input->mouse_y;
    state[2] = g_input->mouse_buttons;
    if (ready)
        *ready = g_input->mouse_ready ? 1 : 0;
}

int dev_ready(void)
{
    return g_dev_ready;
}

u64 dev_alloc_page(void)
{
    u64 physical = pmm_alloc_page();
    if (physical)
        memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
    return physical;
}
