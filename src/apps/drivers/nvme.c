/*
 * Zhaoxu OS - NVMe host driver (Ring 3).
 *
 * A polled NVMe 1.x driver: one admin queue pair performs identify and
 * queue creation, one I/O pair serves the block-device RPC.  The driver runs
 * in the Ring3 driver service with an identity map, so queue and PRP
 * addresses are simply the virtual pointers returned by dev_alloc_page().
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern u64 pmm_alloc_page(void);
extern void buffer_zero(void *destination, u32 bytes);
extern void console_write(const char *text);

#define NVME_QDEPTH 64u
#define NVME_CMD_SIZE 64u
#define NVME_CQE_SIZE 16u

#define NVME_REG_CAP 0x00
#define NVME_REG_VS 0x08
#define NVME_REG_CC 0x14
#define NVME_REG_CSTS 0x1C
#define NVME_REG_AQA 0x24
#define NVME_REG_ASQ 0x28
#define NVME_REG_ACQ 0x30

struct nvme_queue {
    u64 sq_phys;
    u64 cq_phys;
    u16 qid;
    u16 depth;
    u16 tail;
    u16 head;
    u8 phase;
    u16 cid;
};

struct nvme_identify {
    u8 raw[4096];
};

static volatile u8 *g_regs;
static u32 g_doorbell_stride;
static struct nvme_queue g_admin;
static struct nvme_queue g_io;
static u32 g_lba_size;
static u64 g_sectors;
static char g_model[41];
static int g_ready;
static u64 g_bounce_phys;
static u8 *g_bounce;

/* Read a 32-bit controller register. */
static u32 reg_read32(u32 offset)
{
    return *(volatile u32 *)(g_regs + offset);
}

/* Write a 32-bit controller register. */
static void reg_write32(u32 offset, u32 value)
{
    *(volatile u32 *)(g_regs + offset) = value;
}

static u64 reg_read64(u32 offset)
{
    u64 low = reg_read32(offset);
    u64 high = reg_read32(offset + 4);
    return low | (high << 32);
}

static void reg_write64(u32 offset, u64 value)
{
    reg_write32(offset, (u32)value);
    reg_write32(offset + 4, (u32)(value >> 32));
}

/* Ring a queue doorbell at the register block selected by qid and kind. */
static void doorbell(u16 qid, int completion, u16 value)
{
    u32 offset = 0x1000 + ((u32)qid * 2 + (completion ? 1 : 0)) *
                              g_doorbell_stride;
    *(volatile u32 *)(g_regs + offset) = value;
}

/* Submit one 64-byte command and poll for its completion. */
static int submit(struct nvme_queue *queue, const u32 command[16],
                  u32 *result)
{
    u8 *sq = (u8 *)(uintptr_t)queue->sq_phys;
    u8 *cq = (u8 *)(uintptr_t)queue->cq_phys;
    u8 *slot = sq + (u32)queue->tail * NVME_CMD_SIZE;
    for (int i = 0; i < 16; i++)
        ((u32 *)slot)[i] = command[i];
    queue->tail = (u16)((queue->tail + 1) % queue->depth);
    __sync_synchronize();
    doorbell(queue->qid, 0, queue->tail);

    for (u32 spin = 0; spin < 20000000u; spin++) {
        u8 *entry = cq + (u32)queue->head * NVME_CQE_SIZE;
        u16 status = *(volatile u16 *)(entry + 14);
        if ((status & 1) != queue->phase)
            continue;
        if (result)
            *result = *(u32 *)entry;
        u16 code = (u16)(status >> 1);
        queue->head = (u16)((queue->head + 1) % queue->depth);
        if (queue->head == 0)
            queue->phase ^= 1;
        __sync_synchronize();
        doorbell(queue->qid, 1, queue->head);
        return code ? -1 : 0;
    }
    return -1;
}

static void command_zero(u32 command[16])
{
    for (int i = 0; i < 16; i++)
        command[i] = 0;
}

/* Identify a controller or namespace into a page-aligned buffer. */
static int identify(u32 nsid, u32 cns, void *buffer)
{
    u32 command[16];
    command_zero(command);
    command[0] = 0x06u | ((u32)(++g_admin.cid) << 16);
    command[1] = nsid;
    command[6] = (u32)(uintptr_t)buffer;
    command[7] = (u32)((u64)(uintptr_t)buffer >> 32);
    command[10] = cns;
    return submit(&g_admin, command, NULL);
}

/* Create an I/O completion queue and submission queue with qid 1. */
static int create_queues(void)
{
    u32 command[16];
    command_zero(command);
    command[0] = 0x05u | ((u32)(++g_admin.cid) << 16);
    command[10] = 1u | ((NVME_QDEPTH - 1) << 16);
    command[11] = 1; /* physically contiguous completion queue */
    if (submit(&g_admin, command, NULL) != 0)
        return -1;

    command_zero(command);
    command[0] = 0x01u | ((u32)(++g_admin.cid) << 16);
    command[10] = 1u | ((NVME_QDEPTH - 1) << 16);
    command[11] = 1; /* physically contiguous submission queue */
    return submit(&g_admin, command, NULL);
}

/* Read or write whole 512-byte sectors through the I/O queue. */
static int io_transfer(int write, u64 lba, u32 count, void *buffer)
{
    if (!g_ready || !count)
        return -1;
    if (lba + count > g_sectors)
        return -1;

    u8 *data = buffer;
    u64 byte = lba * 512;
    u32 remaining = count * 512;

    if (g_lba_size == 512) {
        while (remaining) {
            u32 chunk = remaining > 4096 ? 4096 : remaining;
            u32 command[16];
            command_zero(command);
            command[0] = (write ? 0x01u : 0x02u) |
                         ((u32)(++g_io.cid) << 16);
            command[1] = 1;
            command[6] = (u32)(uintptr_t)data;
            command[7] = (u32)((u64)(uintptr_t)data >> 32);
            command[10] = (u32)(byte / 512);
            command[11] = (u32)((byte / 512) >> 32);
            command[12] = (chunk / 512) - 1;
            if (submit(&g_io, command, NULL) != 0)
                return -1;
            data += chunk;
            byte += chunk;
            remaining -= chunk;
        }
        return 0;
    }

    /* The namespace uses 4 KiB logical blocks; transfer whole blocks and
     * copy the requested window out of the bounce buffer. */
    while (remaining) {
        u64 block = byte / g_lba_size;
        u32 within = (u32)(byte % g_lba_size);
        u32 chunk = g_lba_size - within;
        if (chunk > remaining)
            chunk = remaining;
        u32 command[16];
        command_zero(command);
        command[1] = 1;
        command[6] = (u32)(uintptr_t)g_bounce;
        command[7] = (u32)((u64)(uintptr_t)g_bounce >> 32);
        command[10] = (u32)block;
        command[11] = (u32)(block >> 32);
        command[12] = 0;

        if (write && (within != 0 || chunk != g_lba_size)) {
            /* Partial 4 KiB block: read-modify-write so bytes outside the
             * caller's window keep their old values. */
            command[0] = 0x02u | ((u32)(++g_io.cid) << 16);
            if (submit(&g_io, command, NULL) != 0)
                return -1;
            for (u32 i = 0; i < chunk; i++)
                g_bounce[within + i] = data[i];
            command[0] = 0x01u | ((u32)(++g_io.cid) << 16);
            if (submit(&g_io, command, NULL) != 0)
                return -1;
        } else {
            if (write) {
                for (u32 i = 0; i < chunk; i++)
                    g_bounce[within + i] = data[i];
            }
            command[0] = (write ? 0x01u : 0x02u) |
                         ((u32)(++g_io.cid) << 16);
            if (submit(&g_io, command, NULL) != 0)
                return -1;
            if (!write) {
                for (u32 i = 0; i < chunk; i++)
                    data[i] = g_bounce[within + i];
            }
        }
        data += chunk;
        byte += chunk;
        remaining -= chunk;
    }
    return 0;
}

/* Flush the namespace's volatile write cache. */
static int nvme_flush(void)
{
    if (!g_ready)
        return -1;
    u32 command[16];
    command_zero(command);
    command[0] = 0x00u | ((u32)(++g_io.cid) << 16);
    command[1] = 1;
    return submit(&g_io, command, NULL);
}

int nvme_init(void)
{
    int found = -1;
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && found < 0; bus++) {
        for (u32 slot = 0; slot < 32 && found < 0; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            u32 class = pci_config_read32(bus, slot, 0, 8) >> 8;
            u32 vendor = id & 0xFFFF;
            u32 device = id >> 16;
            int nvme = class == 0x010802u;
            int known = (vendor == 0x1B36 && device == 0x0010);
            if (!nvme && !known)
                continue;

            u32 bar_low = pci_config_read32(bus, slot, 0, 0x10);
            u32 bar_high = pci_config_read32(bus, slot, 0, 0x14);
            bar = (bar_low & 0xFFFFFFF0u) | ((u64)bar_high << 32);
            if (!bar || bar >= 0x100000000ull)
                continue;
            u32 command = pci_config_read32(bus, slot, 0, 4);
            pci_config_write32(bus, slot, 0, 4, command | 0x6);
            found = (int)((bus << 8) | (slot << 3));
        }
    }
    if (found < 0 || !bar)
        return -1;

    g_regs = (volatile u8 *)(uintptr_t)bar;
    u64 cap = reg_read64(NVME_REG_CAP);
    u32 version = reg_read32(NVME_REG_VS);
    g_doorbell_stride = 4u << ((cap >> 32) & 0xF);

    /* Disable the controller and wait for it to acknowledge reset. */
    reg_write32(NVME_REG_CC, 0);
    for (u32 spin = 0; spin < 1000000u; spin++) {
        if ((reg_read32(NVME_REG_CSTS) & 1) == 0)
            break;
    }

    g_admin.sq_phys = pmm_alloc_page();
    g_admin.cq_phys = pmm_alloc_page();
    g_io.sq_phys = pmm_alloc_page();
    g_io.cq_phys = pmm_alloc_page();
    g_bounce_phys = pmm_alloc_page();
    if (!g_admin.sq_phys || !g_admin.cq_phys || !g_io.sq_phys ||
        !g_io.cq_phys || !g_bounce_phys)
        return -1;
    g_bounce = (u8 *)(uintptr_t)g_bounce_phys;
    buffer_zero(g_bounce, 4096);
    buffer_zero((void *)(uintptr_t)g_admin.sq_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_admin.cq_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_io.sq_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_io.cq_phys, 4096);

    g_admin.qid = 0;
    g_admin.depth = NVME_QDEPTH;
    g_admin.tail = 0;
    g_admin.head = 0;
    g_admin.phase = 1;
    g_admin.cid = 0;
    g_io.qid = 1;
    g_io.depth = NVME_QDEPTH;
    g_io.tail = 0;
    g_io.head = 0;
    g_io.phase = 1;
    g_io.cid = 0;

    reg_write32(NVME_REG_AQA, (NVME_QDEPTH - 1) |
                              ((NVME_QDEPTH - 1) << 16));
    reg_write64(NVME_REG_ASQ, g_admin.sq_phys);
    reg_write64(NVME_REG_ACQ, g_admin.cq_phys);
    reg_write32(NVME_REG_CC, 1u | (0u << 4) | (0u << 7) |
                             (0u << 14) | (6u << 16) | (4u << 20));
    int ready = 0;
    for (u32 spin = 0; spin < 10000000u; spin++) {
        if (reg_read32(NVME_REG_CSTS) & 1) {
            ready = 1;
            break;
        }
    }
    if (!ready)
        return -1;

    struct nvme_identify *identify_buffer =
        (struct nvme_identify *)(uintptr_t)pmm_alloc_page();
    if (!identify_buffer)
        return -1;
    buffer_zero(identify_buffer, 4096);
    if (identify(0, 1, identify_buffer) != 0) {
        console_write("[nvme] controller identify failed\n");
        return -1;
    }
    for (int i = 0; i < 40; i++)
        g_model[i] = (char)identify_buffer->raw[24 + i];
    g_model[40] = 0;
    for (int i = 39; i >= 0 && g_model[i] == ' '; i--)
        g_model[i] = 0;

    if (identify(1, 0, identify_buffer) != 0) {
        console_write("[nvme] namespace identify failed\n");
        return -1;
    }
    u64 nsze = 0;
    for (int i = 7; i >= 0; i--)
        nsze = (nsze << 8) | identify_buffer->raw[i];
    u8 flbas = identify_buffer->raw[26] & 0x0F;
    u32 format = 128 + flbas * 4;
    u8 shift = identify_buffer->raw[format + 1] & 0x0F;
    g_lba_size = 1u << shift;
    if (g_lba_size != 512 && g_lba_size != 4096)
        g_lba_size = 512;
    g_sectors = nsze * (g_lba_size / 512);
    if (!g_sectors)
        return -1;

    if (create_queues() != 0) {
        console_write("[nvme] I/O queue creation failed\n");
        return -1;
    }

    g_ready = 1;
    (void)version;
    console_write("[nvme] controller ready, model ");
    console_write(g_model);
    console_write("\n");
    return 0;
}

int nvme_disk_count(void)
{
    return g_ready ? 1 : 0;
}

u64 nvme_disk_sectors(void)
{
    return g_ready ? g_sectors : 0;
}

const char *nvme_disk_model(void)
{
    return g_model;
}

int nvme_disk_read(u64 lba, u32 count, void *buffer)
{
    return io_transfer(0, lba, count, buffer);
}

int nvme_disk_write(u64 lba, u32 count, const void *buffer)
{
    return io_transfer(1, lba, count, (void *)buffer);
}

int nvme_disk_flush(void)
{
    return nvme_flush();
}
