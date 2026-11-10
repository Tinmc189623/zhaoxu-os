/*
 * Zhaoxu OS - Ring3 driver service.
 *
 * Owns every device: PCI config, AHCI storage, PS/2 keyboard and mouse,
 * CMOS RTC and the serial console.  The kernel communicates through the
 * shared dev pages; the service also drains the kernel log ring to COM1.
 */
#include <dev.h>
#include <io.h>
#include <rtc.h>
#include <types.h>
#include <userlib.h>

extern void serial_init(u32 baud);
extern void serial_putc(u8 value);
extern int serial_getc(void);
extern int ahci_init(void);
extern int ahci_disk_count(void);
extern u64 ahci_disk_sectors(int disk);
extern int ahci_disk_port(int disk);
extern const char *ahci_disk_model(int disk);
extern int ahci_disk_read(int disk, u64 lba, u32 count, void *buffer);
extern int ahci_disk_write(int disk, u64 lba, u32 count, const void *buffer);
extern int ahci_disk_flush(int disk);
extern void ps2_init(void);
extern int mouse_init(int width, int height);
extern void mouse_irq(void);
extern int mouse_x(void);
extern int mouse_y(void);
extern int mouse_buttons(void);
extern int mouse_ready(void);
extern int rtc_read(struct rtc_time *time);

void keyboard_poll(void);
void keyboard_init(void);

static struct dev_header *g_header;
static struct dev_input *g_input;
static struct dev_log *g_log;
static u8 *g_data;
static int g_last_mouse_x;
static int g_last_mouse_y;
static int g_last_buttons;

static u64 parse_hex(const char *text)
{
    u64 value = 0;
    if (text && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    for (; text && *text; text++) {
        char c = *text;
        u64 digit;
        if (c >= '0' && c <= '9')
            digit = (u64)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (u64)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (u64)(c - 'A' + 10);
        else
            break;
        value = (value << 4) | digit;
    }
    return value;
}

static void log_drain(void)
{
    if (!g_log)
        return;
    while (g_log->tail != g_log->head) {
        char c = g_log->data[g_log->tail];
        g_log->tail = (g_log->tail + 1) % DEV_LOG_SIZE;
        serial_putc((u8)c);
    }
}

static void input_push(char c)
{
    if (!g_input)
        return;
    u32 head = __atomic_load_n(&g_input->head, __ATOMIC_RELAXED);
    u32 tail = __atomic_load_n(&g_input->tail, __ATOMIC_ACQUIRE);
    u32 next = (head + 1) % sizeof(g_input->keys);
    if (next == tail)
        return;
    g_input->keys[head] = c;
    __atomic_store_n(&g_input->head, next, __ATOMIC_RELEASE);
}

void driver_push_key(char c)
{
    input_push(c);
}

static void input_poll(void)
{
    keyboard_poll();
    int c = serial_getc();
    if (c >= 0) {
        if (c == 13)
            c = 10;
        driver_push_key((char)c);
    }
    mouse_irq();

    int x = mouse_x();
    int y = mouse_y();
    int buttons = mouse_buttons();
    if (x != g_last_mouse_x || y != g_last_mouse_y ||
        buttons != g_last_buttons) {
        g_input->mouse_x = x;
        g_input->mouse_y = y;
        g_input->mouse_buttons = (u32)buttons;
        g_last_mouse_x = x;
        g_last_mouse_y = y;
        g_last_buttons = buttons;
    }
    g_input->mouse_ready = (u32)mouse_ready();
}

static void fill_info(void)
{
    int count = ahci_disk_count();
    if (count < 0)
        count = 0;
    if (count > 8)
        count = 8;
    g_header->disk_count = (u32)count;
    for (int i = 0; i < count; i++) {
        g_header->ports[i] = (u32)ahci_disk_port(i);
        g_header->sectors[i] = ahci_disk_sectors(i);
        const char *model = ahci_disk_model(i);
        usize n = 0;
        while (model && model[n] && n + 1 < sizeof(g_header->models[i])) {
            g_header->models[i][n] = model[n];
            n++;
        }
        g_header->models[i][n] = 0;
    }
}

static int handle_request(void)
{
    if (g_header->state != DEV_STATE_REQUEST)
        return 0;

    __sync_synchronize();
    int status = 0;
    switch (g_header->op) {
    case DEV_OP_READ:
        status = ahci_disk_read((int)g_header->disk, g_header->lba,
                                g_header->count, g_data);
        break;
    case DEV_OP_WRITE:
        status = ahci_disk_write((int)g_header->disk, g_header->lba,
                                 g_header->count, g_data);
        break;
    case DEV_OP_FLUSH:
        status = ahci_disk_flush((int)g_header->disk);
        break;
    case DEV_OP_INFO:
        fill_info();
        break;
    case DEV_OP_RTC:
        status = rtc_read((struct rtc_time *)g_data);
        break;
    default:
        status = -1;
        break;
    }
    g_header->status = status;
    __sync_synchronize();
    g_header->state = DEV_STATE_DONE;
    __sync_synchronize();
    return 1;
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    if (argc < 1)
        return 1;

    u64 header_phys = parse_hex(argv[0]);
    g_header = (struct dev_header *)(u64)header_phys;
    if (g_header->magic != DEV_MAGIC)
        return 2;
    g_data = (u8 *)(u64)g_header->data_phys;
    g_input = (struct dev_input *)(u64)g_header->input_phys;
    g_log = (struct dev_log *)(u64)g_header->log_phys;

    serial_init(115200);
    serial_putc('\r');
    serial_putc('\n');
    log_drain();

    ps2_init();
    keyboard_init();
    int mouse = mouse_init((int)g_header->width, (int)g_header->height);
    if (mouse > 0) {
        g_last_mouse_x = mouse_x();
        g_last_mouse_y = mouse_y();
    }

    int disks = ahci_init();
    if (disks < 0)
        disks = ahci_disk_count();
    (void)disks;
    fill_info();

    g_header->state = DEV_STATE_READY;
    __sync_synchronize();

    for (;;) {
        log_drain();
        input_poll();
        if (!handle_request())
            sys_sleep(1);
    }
}
