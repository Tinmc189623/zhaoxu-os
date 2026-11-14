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
extern int usb_msc_init(void);
extern u64 usb_msc_disk_sectors(int disk);
extern int usb_msc_disk_port(int disk);
extern const char *usb_msc_disk_model(int disk);
extern int usb_msc_disk_read(int disk, u64 lba, u32 count, void *buffer);
extern int usb_msc_disk_write(int disk, u64 lba, u32 count,
                              const void *buffer);
extern int usb_msc_disk_flush(int disk);
extern int nvme_init(void);
extern int nvme_disk_count(void);
extern u64 nvme_disk_sectors(void);
extern const char *nvme_disk_model(void);
extern int nvme_disk_read(u64 lba, u32 count, void *buffer);
extern int nvme_disk_write(u64 lba, u32 count, const void *buffer);
extern int nvme_disk_flush(void);
extern int sdhci_init(void);
extern int sdhci_disk_count(void);
extern u64 sdhci_disk_sectors(void);
extern const char *sdhci_disk_model(void);
extern int sdhci_disk_read(u64 lba, u32 count, void *buffer);
extern int sdhci_disk_write(u64 lba, u32 count, const void *buffer);
extern int sdhci_disk_flush(void);
extern int ide_init(void);
extern int ide_drive_count(void);
extern u64 ide_drive_sectors(int drive);
extern int ide_drive_bus(int drive);
extern int ide_drive_unit(int drive);
extern const char *ide_drive_model(int drive);
extern int ide_drive_read(int drive, u64 lba, u32 count, void *buffer);
extern int e1000_init(void);
extern int hda_init(void);
extern int ehci_init(void);
extern int ohci_init(void);
extern int i2c_dw_init(void);
extern int spi_pch_init(void);
extern int thermal_init(void);
extern void thermal_poll(struct dev_header *header);
extern int virtio_gpu_init(void);
extern void ps2_init(void);
extern u8 ps2_status(void);
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
static int g_ahci_disks;
static int g_nvme_disks;
static int g_sd_disks;
static int g_ide_disks;
static int g_usb_disks;

/* Device order inside dev_header: AHCI, NVMe, SD/MMC, IDE, USB storage.
 * The header has room for sixteen disks. */
static int disk_total(void)
{
    int total = g_ahci_disks + g_nvme_disks + g_sd_disks + g_ide_disks +
                g_usb_disks;
    return total > 16 ? 16 : total;
}

static int disk_is_nvme(int disk)
{
    return disk >= g_ahci_disks &&
           disk < g_ahci_disks + g_nvme_disks;
}

static int disk_is_sd(int disk)
{
    int base = g_ahci_disks + g_nvme_disks;
    return disk >= base && disk < base + g_sd_disks;
}

static int disk_is_ide(int disk)
{
    int base = g_ahci_disks + g_nvme_disks + g_sd_disks;
    return disk >= base && disk < base + g_ide_disks;
}

static int disk_is_usb(int disk)
{
    int base = g_ahci_disks + g_nvme_disks + g_sd_disks + g_ide_disks;
    return disk >= base;
}

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
    /* Drain the 8042 completely each pass: a 1-byte controller buffer loses
     * mouse packets if we only read one byte per millisecond. */
    for (int i = 0; i < 64; i++) {
        u8 status = ps2_status();
        if ((status & 1) == 0)
            break;
        if (status & 0x20)
            mouse_irq();
        else
            keyboard_poll();
    }

    for (int i = 0; i < 32; i++) {
        int c = serial_getc();
        if (c < 0)
            break;
        if (c == 13)
            c = 10;
        driver_push_key((char)c);
    }

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

static void copy_model(int index, const char *model)
{
    usize n = 0;
    while (model && model[n] && n + 1 < sizeof(g_header->models[index])) {
        g_header->models[index][n] = model[n];
        n++;
    }
    g_header->models[index][n] = 0;
}

static void copy_model_ext(int index, const char *model)
{
    usize n = 0;
    while (model && model[n] &&
           n + 1 < sizeof(g_header->models_ext[index])) {
        g_header->models_ext[index][n] = model[n];
        n++;
    }
    g_header->models_ext[index][n] = 0;
}

static void fill_info(void)
{
    int count = disk_total();
    int usb_base = g_ahci_disks + g_nvme_disks + g_sd_disks + g_ide_disks;
    int ide_base = g_ahci_disks + g_nvme_disks + g_sd_disks;
    g_header->disk_count = (u32)count;
    for (int i = 0; i < count; i++) {
        const char *model = NULL;
        int port = -1;
        u64 sectors = 0;
        if (disk_is_nvme(i)) {
            sectors = nvme_disk_sectors();
            model = nvme_disk_model();
        } else if (disk_is_sd(i)) {
            sectors = sdhci_disk_sectors();
            model = sdhci_disk_model();
        } else if (disk_is_ide(i)) {
            int drive = i - ide_base;
            port = ide_drive_bus(drive);
            sectors = ide_drive_sectors(drive);
            model = ide_drive_model(drive);
        } else if (disk_is_usb(i)) {
            int usb = i - usb_base;
            port = usb_msc_disk_port(usb);
            sectors = usb_msc_disk_sectors(usb);
            model = usb_msc_disk_model(usb);
        } else {
            port = ahci_disk_port(i);
            sectors = ahci_disk_sectors(i);
            model = ahci_disk_model(i);
        }
        if (i < 8) {
            g_header->ports[i] = (u32)port;
            g_header->sectors[i] = sectors;
            copy_model(i, model ? model : "disk");
        } else {
            g_header->ports_ext[i - 8] = (u32)port;
            g_header->sectors_ext[i - 8] = sectors;
            copy_model_ext(i - 8, model ? model : "disk");
        }
    }
}

static int block_read(int disk, u64 lba, u32 count, void *buffer)
{
    int usb_base = g_ahci_disks + g_nvme_disks + g_sd_disks + g_ide_disks;
    int ide_base = g_ahci_disks + g_nvme_disks + g_sd_disks;
    if (disk_is_nvme(disk))
        return nvme_disk_read(lba, count, buffer);
    if (disk_is_sd(disk))
        return sdhci_disk_read(lba, count, buffer);
    if (disk_is_ide(disk))
        return ide_drive_read(disk - ide_base, lba, count, buffer);
    if (disk_is_usb(disk))
        return usb_msc_disk_read(disk - usb_base, lba, count, buffer);
    return ahci_disk_read(disk, lba, count, buffer);
}

static int block_write(int disk, u64 lba, u32 count, const void *buffer)
{
    int usb_base = g_ahci_disks + g_nvme_disks + g_sd_disks + g_ide_disks;
    if (disk_is_nvme(disk))
        return nvme_disk_write(lba, count, buffer);
    if (disk_is_sd(disk))
        return sdhci_disk_write(lba, count, buffer);
    if (disk_is_ide(disk))
        return -1; /* the IDE driver is read-only */
    if (disk_is_usb(disk))
        return usb_msc_disk_write(disk - usb_base, lba, count, buffer);
    return ahci_disk_write(disk, lba, count, buffer);
}

static int block_flush(int disk)
{
    int usb_base = g_ahci_disks + g_nvme_disks + g_sd_disks + g_ide_disks;
    if (disk_is_nvme(disk))
        return nvme_disk_flush();
    if (disk_is_sd(disk))
        return sdhci_disk_flush();
    if (disk_is_ide(disk))
        return 0;
    if (disk_is_usb(disk))
        return usb_msc_disk_flush(disk - usb_base);
    return ahci_disk_flush(disk);
}

static int handle_request(void)
{
    if (g_header->state != DEV_STATE_REQUEST)
        return 0;

    __sync_synchronize();
    int status = 0;
    int disk = (int)g_header->disk;
    switch (g_header->op) {
    case DEV_OP_READ:
        status = disk >= 0 && disk < disk_total()
                     ? block_read(disk, g_header->lba, g_header->count, g_data)
                     : -1;
        break;
    case DEV_OP_WRITE:
        status = disk >= 0 && disk < disk_total()
                     ? block_write(disk, g_header->lba, g_header->count,
                                   g_data)
                     : -1;
        break;
    case DEV_OP_FLUSH:
        status = disk >= 0 && disk < disk_total()
                     ? block_flush(disk)
                     : -1;
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
    if (!g_header || g_header->magic != DEV_MAGIC)
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

    (void)ahci_init();
    g_ahci_disks = ahci_disk_count();
    if (g_ahci_disks < 0)
        g_ahci_disks = 0;

    g_nvme_disks = nvme_init() == 0 ? nvme_disk_count() : 0;
    if (g_nvme_disks < 0)
        g_nvme_disks = 0;
    g_sd_disks = sdhci_init() == 0 ? sdhci_disk_count() : 0;
    if (g_sd_disks < 0)
        g_sd_disks = 0;
    g_ide_disks = ide_init() == 0 ? ide_drive_count() : 0;
    if (g_ide_disks < 0)
        g_ide_disks = 0;

    g_usb_disks = usb_msc_init();
    if (g_usb_disks < 0)
        g_usb_disks = 0;

    /* Network, audio, USB 1.1/2.0 companions, buses and accelerators are
     * optional: each driver returns -1 when its hardware is absent. */
    (void)e1000_init();
    (void)ehci_init();
    (void)ohci_init();
    (void)i2c_dw_init();
    (void)spi_pch_init();
    (void)thermal_init();
    (void)hda_init();
    /* M1: the control queue is owned by /bin/display's KMS service. */

    fill_info();

    g_header->state = DEV_STATE_READY;
    __sync_synchronize();

    int spin = sys_cpus() > 1;

    for (;;) {
        log_drain();
        input_poll();
        thermal_poll(g_header);
        if (!handle_request() && !spin)
            sys_sleep(1);
    }
}
