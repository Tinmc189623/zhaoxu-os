/*
 * Zhaoxu OS - AHCI (SATA) block device driver
 *
 * The kernel identity maps the first 4 GiB of physical memory, so the HBA
 * MMIO window and all DMA structures are accessed through their physical
 * addresses.  Each command moves at most one 4 KiB page through a bounce
 * buffer, which keeps the PRDT trivial and tolerates non-contiguous kernel
 * heap buffers.
 */
#include <ahci.h>
#include <kprintf.h>
#include <mem.h>
#include <pci.h>
#include <string.h>
#include <types.h>

#define AHCI_MAX_DISKS 8
#define AHCI_MAX_PORTS 32
#define AHCI_SECTOR_SIZE 512
#define AHCI_BOUNCE_SECTORS 8

#define HBA_CAP        0x00
#define HBA_GHC        0x04
#define HBA_PI         0x0C
#define HBA_VERSION    0x10

#define PORT_CLB       0x00
#define PORT_CLBU      0x04
#define PORT_FB        0x08
#define PORT_FBU       0x0C
#define PORT_IS        0x10
#define PORT_CMD       0x18
#define PORT_TFD       0x20
#define PORT_SIG       0x24
#define PORT_SSTS      0x28
#define PORT_SCTL      0x2C
#define PORT_SERR      0x30
#define PORT_CI        0x38

#define PORT_CMD_ST    (1u << 0)
#define PORT_CMD_SUD   (1u << 1)
#define PORT_CMD_FRE   (1u << 4)
#define PORT_CMD_FR    (1u << 14)
#define PORT_CMD_CR    (1u << 15)

#define PORT_IS_TFES   (1u << 30)
#define PORT_TFD_ERR   (1u << 0)

struct ahci_port {
    int port;
    u64 sectors;
    u64 clb_phys;
    u64 fis_phys;
    u64 ct_phys;
    u64 bounce_phys;
    u8 *clb;
    u8 *fis;
    u8 *ct;
    u8 *bounce;
};

static struct ahci_port g_disks[AHCI_MAX_DISKS];
static int g_disk_count;
static volatile u8 *g_hba;
static u8 g_controller_bus;
static u8 g_controller_slot;
static u8 g_controller_function;

static inline u32 hba_read(u32 offset)
{
    return *(volatile u32 *)(g_hba + offset);
}

static inline void hba_write(u32 offset, u32 value)
{
    *(volatile u32 *)(g_hba + offset) = value;
}

static inline u32 port_read(int port, u32 offset)
{
    return *(volatile u32 *)(g_hba + 0x100 + (u32)port * 0x80 + offset);
}

static inline void port_write(int port, u32 offset, u32 value)
{
    *(volatile u32 *)(g_hba + 0x100 + (u32)port * 0x80 + offset) = value;
}

static int find_controller(void)
{
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 slot = 0; slot < 32; slot++) {
            for (u32 function = 0; function < 8; function++) {
                u32 id = pci_config_read32((u8)bus, (u8)slot,
                                           (u8)function, 0x00);
                if ((id & 0xFFFF) == 0xFFFF)
                    continue;
                u32 class_reg = pci_config_read32((u8)bus, (u8)slot,
                                                  (u8)function, 0x08);
                u8 class_code = (u8)(class_reg >> 24);
                u8 subclass = (u8)(class_reg >> 16);
                u8 prog_if = (u8)(class_reg >> 8);
                if (class_code == 0x01 && subclass == 0x06 &&
                    prog_if == 0x01) {
                    g_controller_bus = (u8)bus;
                    g_controller_slot = (u8)slot;
                    g_controller_function = (u8)function;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static u64 read_abar(void)
{
    u32 bar = pci_config_read32(g_controller_bus, g_controller_slot,
                                g_controller_function, 0x24);
    if (bar & 1)
        return 0;
    if ((bar & 0x06) == 0x04) {
        u32 high = pci_config_read32(g_controller_bus, g_controller_slot,
                                     g_controller_function, 0x28);
        return ((u64)high << 32) | (u64)(bar & ~0xFULL);
    }
    return (u64)(bar & ~0xFULL);
}

static int port_stop(int port)
{
    u32 command = port_read(port, PORT_CMD);
    command &= ~PORT_CMD_ST;
    port_write(port, PORT_CMD, command);
    for (u32 i = 0; i < 1000000; i++) {
        if (!(port_read(port, PORT_CMD) & PORT_CMD_CR))
            break;
        __asm__ volatile("pause");
    }
    if (port_read(port, PORT_CMD) & PORT_CMD_CR)
        return -1;

    command = port_read(port, PORT_CMD);
    command &= ~PORT_CMD_FRE;
    port_write(port, PORT_CMD, command);
    for (u32 i = 0; i < 1000000; i++) {
        if (!(port_read(port, PORT_CMD) & PORT_CMD_FR))
            break;
        __asm__ volatile("pause");
    }
    return (port_read(port, PORT_CMD) & PORT_CMD_FR) ? -1 : 0;
}

static int port_start(struct ahci_port *disk)
{
    int port = disk->port;

    port_write(port, PORT_CLB, (u32)disk->clb_phys);
    port_write(port, PORT_CLBU, (u32)(disk->clb_phys >> 32));
    port_write(port, PORT_FB, (u32)disk->fis_phys);
    port_write(port, PORT_FBU, (u32)(disk->fis_phys >> 32));
    port_write(port, PORT_IS, 0xFFFFFFFF);
    port_write(port, PORT_SERR, 0xFFFFFFFF);

    u32 command = port_read(port, PORT_CMD);
    command |= PORT_CMD_FRE;
    port_write(port, PORT_CMD, command);
    for (u32 i = 0; i < 1000000; i++) {
        if (port_read(port, PORT_CMD) & PORT_CMD_FR)
            break;
        __asm__ volatile("pause");
    }
    if (!(port_read(port, PORT_CMD) & PORT_CMD_FR))
        return -1;

    command = port_read(port, PORT_CMD);
    command |= PORT_CMD_ST | PORT_CMD_SUD;
    port_write(port, PORT_CMD, command);
    for (u32 i = 0; i < 1000000; i++) {
        if (port_read(port, PORT_CMD) & PORT_CMD_CR)
            break;
        __asm__ volatile("pause");
    }
    if (!(port_read(port, PORT_CMD) & PORT_CMD_CR))
        return -1;
    return 0;
}

static int ahci_command(struct ahci_port *disk, u8 command, u64 lba,
                        u16 count, u64 buffer_phys, u32 bytes, int write)
{
    int port = disk->port;

    memset(disk->clb, 0, 1024);
    memset(disk->ct, 0, 4096);

    u8 *fis = disk->ct;
    fis[0] = 0x27;                      /* H2D register FIS */
    fis[1] = 0x80;                      /* command flag */
    fis[2] = command;
    fis[3] = 0;
    fis[4] = (u8)(lba & 0xFF);
    fis[5] = (u8)((lba >> 8) & 0xFF);
    fis[6] = (u8)((lba >> 16) & 0xFF);
    fis[7] = 0x40;                      /* LBA mode */
    fis[8] = (u8)((lba >> 24) & 0xFF);
    fis[9] = (u8)((lba >> 32) & 0xFF);
    fis[10] = (u8)((lba >> 40) & 0xFF);
    fis[11] = 0;
    fis[12] = (u8)(count & 0xFF);
    fis[13] = (u8)((count >> 8) & 0xFF);
    fis[14] = 0;
    fis[15] = 0;

    u32 *prdt = (u32 *)(disk->ct + 0x80);
    prdt[0] = (u32)(buffer_phys & 0xFFFFFFFF);
    prdt[1] = (u32)(buffer_phys >> 32);
    prdt[2] = 0;
    prdt[3] = bytes - 1;

    u32 *header = (u32 *)disk->clb;
    header[0] = 5u | (1u << 16) | (write ? (1u << 6) : 0u) | (1u << 7);
    header[1] = 0;
    header[2] = (u32)(disk->ct_phys & 0xFFFFFFFF);
    header[3] = (u32)(disk->ct_phys >> 32);

    port_write(port, PORT_IS, 0xFFFFFFFF);
    port_write(port, PORT_SERR, 0xFFFFFFFF);
    __asm__ volatile("mfence" ::: "memory");
    port_write(port, PORT_CI, 1);

    int timeout = 20000000;
    while ((port_read(port, PORT_CI) & 1) && timeout-- > 0)
        __asm__ volatile("pause");
    if (timeout <= 0)
        return -1;

    u32 status = port_read(port, PORT_IS);
    if (status & PORT_IS_TFES)
        return -1;
    if (port_read(port, PORT_TFD) & PORT_TFD_ERR)
        return -1;
    return 0;
}

static int port_identify(struct ahci_port *disk, u16 *identify)
{
    memset(disk->bounce, 0, AHCI_SECTOR_SIZE);
    if (ahci_command(disk, 0xEC, 0, 0, disk->bounce_phys,
                     AHCI_SECTOR_SIZE, 0) != 0)
        return -1;
    memcpy(identify, disk->bounce, AHCI_SECTOR_SIZE);
    return 0;
}

static int port_probe(int port)
{
    if (g_disk_count >= AHCI_MAX_DISKS)
        return 0;

    u32 ssts = port_read(port, PORT_SSTS);
    if ((ssts & 0x0F) != 3)              /* DET: device present */
        return 0;

    struct ahci_port *disk = &g_disks[g_disk_count];
    memset(disk, 0, sizeof(*disk));
    disk->port = port;

    disk->clb = (u8 *)(uintptr_t)pmm_alloc_page();
    disk->fis = (u8 *)(uintptr_t)pmm_alloc_page();
    disk->ct = (u8 *)(uintptr_t)pmm_alloc_page();
    disk->bounce = (u8 *)(uintptr_t)pmm_alloc_page();
    if (!disk->clb || !disk->fis || !disk->ct || !disk->bounce) {
        kprintf("[ahci] port %d: DMA page allocation failed\n", port);
        return 0;
    }

    disk->clb_phys = (u64)(uintptr_t)disk->clb;
    disk->fis_phys = (u64)(uintptr_t)disk->fis;
    disk->ct_phys = (u64)(uintptr_t)disk->ct;
    disk->bounce_phys = (u64)(uintptr_t)disk->bounce;

    if (port_stop(port) != 0) {
        kprintf("[ahci] port %d: engine did not stop\n", port);
        return 0;
    }
    if (port_start(disk) != 0) {
        kprintf("[ahci] port %d: engine did not start\n", port);
        return 0;
    }

    u16 identify[256];
    if (port_identify(disk, identify) != 0) {
        kprintf("[ahci] port %d: IDENTIFY failed\n", port);
        return 0;
    }

    u64 sectors = ((u64)identify[103] << 48) |
                  ((u64)identify[102] << 32) |
                  ((u64)identify[101] << 16) |
                  (u64)identify[100];
    if (sectors == 0)
        sectors = ((u32)identify[61] << 16) | identify[60];
    if (sectors == 0) {
        kprintf("[ahci] port %d: drive reports zero sectors\n", port);
        return 0;
    }

    disk->sectors = sectors;

    char model[41];
    for (int i = 0; i < 40; i++)
        model[i] = (char)(identify[27 + i / 2] >> (8 * (1 - (i & 1))));
    model[40] = 0;
    for (int i = 39; i >= 0 && (model[i] == ' ' || model[i] == 0); i--)
        model[i] = 0;

    kprintf("[ahci] port %d: %s, %llu sectors (%llu MiB)\n",
            port, model, sectors, (sectors * AHCI_SECTOR_SIZE) >> 20);

    g_disk_count++;
    return 1;
}

int ahci_init(void)
{
    g_disk_count = 0;
    g_hba = 0;

    if (!find_controller()) {
        kprintf("[ahci] no AHCI controller found\n");
        return -1;
    }

    u64 abar = read_abar();
    if (!abar || abar >= 0x100000000ULL) {
        kprintf("[ahci] unusable ABAR 0x%llx\n", abar);
        return -1;
    }

    u32 command = pci_config_read32(g_controller_bus, g_controller_slot,
                                    g_controller_function, 0x04);
    pci_config_write32(g_controller_bus, g_controller_slot,
                       g_controller_function, 0x04,
                       (command & 0xFFFFu) | 0x0006u);

    g_hba = (volatile u8 *)(uintptr_t)abar;

    u32 cap = hba_read(HBA_CAP);
    u32 ghc = hba_read(HBA_GHC);
    ghc |= (1u << 31);                   /* AHCI enable */
    hba_write(HBA_GHC, ghc);

    kprintf("[ahci] controller %02x:%02x.%u, ABAR 0x%llx, %u ports, v%u.%u\n",
            g_controller_bus, g_controller_slot, g_controller_function,
            abar, (cap & 0x1F) + 1,
            (hba_read(HBA_VERSION) >> 16) & 0xFFFF,
            hba_read(HBA_VERSION) & 0xFFFF);

    u32 implemented = hba_read(HBA_PI);
    for (int port = 0; port < AHCI_MAX_PORTS; port++) {
        if (!(implemented & (1u << port)))
            continue;
        port_probe(port);
    }

    kprintf("[ahci] %d block device(s) online\n", g_disk_count);
    return g_disk_count > 0 ? 0 : -1;
}

int ahci_disk_count(void)
{
    return g_disk_count;
}

u64 ahci_disk_sectors(int disk)
{
    if (disk < 0 || disk >= g_disk_count)
        return 0;
    return g_disks[disk].sectors;
}

int ahci_disk_read(int disk, u64 lba, u32 count, void *buffer)
{
    if (disk < 0 || disk >= g_disk_count || !buffer)
        return -1;

    struct ahci_port *entry = &g_disks[disk];
    u8 *destination = (u8 *)buffer;

    while (count > 0) {
        u32 chunk = count > AHCI_BOUNCE_SECTORS ? AHCI_BOUNCE_SECTORS : count;
        u32 bytes = chunk * AHCI_SECTOR_SIZE;
        if (ahci_command(entry, 0x25, lba, (u16)chunk,
                         entry->bounce_phys, bytes, 0) != 0)
            return -1;
        memcpy(destination, entry->bounce, bytes);
        destination += bytes;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}
