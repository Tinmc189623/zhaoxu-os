/*
 * Zhaoxu OS - anonymous shared-memory files and their mappings.
 *
 * memfd is the wl_shm backing store: a client ftruncates it, mmaps it and
 * writes pixels; the descriptor can then be passed to the compositor over
 * SCM_RIGHTS.  Pages are reference-counted by the memfd object; every mmap
 * takes one object reference and unmaps before dropping it.
 */
#include "../proc/process_internal.h"

#include <mem.h>
#include <poll.h>
#include <spinlock.h>
#include <string.h>
#include <types.h>
#include <memfd.h>

#define MEMFD_MAX_PAGES (MEMFD_MAX_BYTES / PAGE_SIZE)
#define MEMFD_MAX_OBJECTS 16
#define MEMFD_MAX_MAPPINGS 64
#define MEMFD_VA_BASE 0x0000010080000000ULL   /* 1 TiB + 2 GiB */
#define MEMFD_VA_LIMIT 0x00000100E0000000ULL  /* 1 TiB + 3.5 GiB */

struct memfd {
    int refs;
    u64 size;
    u32 pages;
    char name[32];
    u64 physical[MEMFD_MAX_PAGES];
};

struct memfd_mapping {
    int used;
    u64 pid;
    u64 address;
    u32 pages;
    struct memfd *memfd;
    struct address_space *space;
};

static struct memfd_mapping g_mappings[MEMFD_MAX_MAPPINGS];
static spinlock_t g_memfd_lock = SPINLOCK_INIT;
static u64 g_memfd_va = MEMFD_VA_BASE;
static u64 g_memfd_pages;
static u64 g_memfd_budget_pages;

/* Bound the shared-memory pool to a quarter of usable RAM instead of a
 * fixed byte count.  A single client cannot ftruncate memfds until the
 * system runs out of pages. */
static u64 memfd_budget(void)
{
    if (!g_memfd_budget_pages) {
        g_memfd_budget_pages = (pmm_managed_bytes() / 4) / PAGE_SIZE;
        if (g_memfd_budget_pages < 64)
            g_memfd_budget_pages = 64;
    }
    return g_memfd_budget_pages;
}

int memfd_create(const char *name, struct memfd **out)
{
    if (!out)
        return -1;
    struct memfd *memfd = kmalloc(sizeof(struct memfd));
    if (!memfd)
        return -1;
    memset(memfd, 0, sizeof(*memfd));
    memfd->refs = 1;
    if (name) {
        strncpy(memfd->name, name, sizeof(memfd->name) - 1);
        memfd->name[sizeof(memfd->name) - 1] = 0;
    }
    *out = memfd;
    return 0;
}

void memfd_ref(struct memfd *memfd)
{
    if (!memfd)
        return;
    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    memfd->refs++;
    spin_unlock_irqrestore(&g_memfd_lock, lock);
}

void memfd_release(struct memfd *memfd)
{
    if (!memfd)
        return;
    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    if (memfd->refs <= 0) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return;
    }
    memfd->refs--;
    if (memfd->refs > 0) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return;
    }
    u32 pages = memfd->pages;
    u64 physical[MEMFD_MAX_PAGES];
    for (u32 i = 0; i < pages; i++)
        physical[i] = memfd->physical[i];
    memfd->pages = 0;
    g_memfd_pages = g_memfd_pages >= pages ? g_memfd_pages - pages : 0;
    spin_unlock_irqrestore(&g_memfd_lock, lock);

    for (u32 i = 0; i < pages; i++)
        pmm_free_page(physical[i]);
    kfree(memfd);
}

int memfd_truncate(struct memfd *memfd, u64 size)
{
    if (!memfd || size > MEMFD_MAX_BYTES)
        return -1;
    u32 wanted = (u32)((size + PAGE_SIZE - 1) / PAGE_SIZE);

    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    /* A shrink must not free a page another process still has mapped.
     * Real memfd pins pages until munmap; refusing the shrink is the
     * conservative equivalent and keeps the mapping valid. */
    for (int i = 0; i < MEMFD_MAX_MAPPINGS; i++) {
        struct memfd_mapping *mapping = &g_mappings[i];
        if (mapping->used && mapping->memfd == memfd &&
            mapping->pages > wanted) {
            spin_unlock_irqrestore(&g_memfd_lock, lock);
            return -1;
        }
    }
    if (wanted > memfd->pages) {
        u32 delta = wanted - memfd->pages;
        if (g_memfd_pages + delta > memfd_budget()) {
            spin_unlock_irqrestore(&g_memfd_lock, lock);
            return -1;
        }
        for (u32 i = memfd->pages; i < wanted; i++) {
            u64 physical = pmm_alloc_page();
            if (!physical) {
                for (u32 undo = memfd->pages; undo < i; undo++) {
                    pmm_free_page(memfd->physical[undo]);
                    memfd->physical[undo] = 0;
                }
                spin_unlock_irqrestore(&g_memfd_lock, lock);
                return -1;
            }
            memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
            memfd->physical[i] = physical;
        }
        memfd->pages = wanted;
        g_memfd_pages += delta;
    } else if (wanted < memfd->pages) {
        u32 delta = memfd->pages - wanted;
        for (u32 i = wanted; i < memfd->pages; i++) {
            if (memfd->physical[i])
                pmm_free_page(memfd->physical[i]);
            memfd->physical[i] = 0;
        }
        memfd->pages = wanted;
        g_memfd_pages = g_memfd_pages >= delta ? g_memfd_pages - delta : 0;
    }
    memfd->size = size;
    spin_unlock_irqrestore(&g_memfd_lock, lock);
    return 0;
}

int memfd_read(struct memfd *memfd, u64 *offset, void *buffer, u64 length)
{
    if (!memfd || !offset || !buffer)
        return -1;
    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    if (*offset >= memfd->size) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return 0;
    }
    u64 want = length;
    if (want > memfd->size - *offset)
        want = memfd->size - *offset;
    u8 *destination = buffer;
    u64 copied = 0;
    while (copied < want) {
        u64 at = *offset + copied;
        u32 page = (u32)(at / PAGE_SIZE);
        u32 within = (u32)(at % PAGE_SIZE);
        u32 chunk = (u32)(PAGE_SIZE - within);
        if (chunk > want - copied)
            chunk = (u32)(want - copied);
        memcpy(destination + copied,
               (const void *)(uintptr_t)(memfd->physical[page] + within),
               chunk);
        copied += chunk;
    }
    *offset += copied;
    spin_unlock_irqrestore(&g_memfd_lock, lock);
    return (int)copied;
}

int memfd_write(struct memfd *memfd, u64 *offset, const void *buffer,
                u64 length)
{
    if (!memfd || !offset || !buffer)
        return -1;
    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    if (*offset >= memfd->size) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return 0;
    }
    u64 want = length;
    if (want > memfd->size - *offset)
        want = memfd->size - *offset;
    const u8 *source = buffer;
    u64 copied = 0;
    while (copied < want) {
        u64 at = *offset + copied;
        u32 page = (u32)(at / PAGE_SIZE);
        u32 within = (u32)(at % PAGE_SIZE);
        u32 chunk = (u32)(PAGE_SIZE - within);
        if (chunk > want - copied)
            chunk = (u32)(want - copied);
        memcpy((void *)(uintptr_t)(memfd->physical[page] + within),
               source + copied, chunk);
        copied += chunk;
    }
    *offset += copied;
    spin_unlock_irqrestore(&g_memfd_lock, lock);
    return (int)copied;
}

int memfd_seek(struct memfd *memfd, u64 *offset, i64 delta, int whence)
{
    if (!memfd || !offset)
        return -1;
    i64 base = 0;
    if (whence == 0)
        base = 0;
    else if (whence == 1)
        base = (i64)*offset;
    else if (whence == 2)
        base = (i64)memfd->size;
    else
        return -1;
    i64 target = base + delta;
    if (target < 0 || (u64)target > memfd->size)
        return -1;
    *offset = (u64)target;
    return (int)target;
}

/* Map the first `length` bytes of the memfd into the caller's address space.
 * wl_shm always maps a whole pool at offset zero, so offsets are rejected. */
u64 memfd_mmap(struct memfd *memfd, struct process *process, u64 length,
               u64 offset)
{
    if (!memfd || !process || !process->space || length == 0 || offset != 0)
        return 0;

    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    if (length > memfd->size) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return 0;
    }
    u32 pages = (u32)((length + PAGE_SIZE - 1) / PAGE_SIZE);
    if (pages > memfd->pages) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return 0;
    }

    struct memfd_mapping *slot = NULL;
    for (int i = 0; i < MEMFD_MAX_MAPPINGS; i++) {
        if (!g_mappings[i].used) {
            slot = &g_mappings[i];
            break;
        }
    }
    if (!slot || g_memfd_va + (u64)pages * PAGE_SIZE > MEMFD_VA_LIMIT) {
        spin_unlock_irqrestore(&g_memfd_lock, lock);
        return 0;
    }

    u64 address = g_memfd_va;
    for (u32 i = 0; i < pages; i++) {
        if (vmm_space_map_page(process->space, address + (u64)i * PAGE_SIZE,
                               memfd->physical[i],
                               VMM_PRESENT | VMM_WRITABLE | VMM_USER |
                                   VMM_EXECUTE_DISABLE) != 0) {
            for (u32 undo = 0; undo < i; undo++)
                vmm_space_unmap_page(process->space,
                                     address + (u64)undo * PAGE_SIZE);
            spin_unlock_irqrestore(&g_memfd_lock, lock);
            return 0;
        }
    }
    g_memfd_va += (u64)pages * PAGE_SIZE;

    slot->used = 1;
    slot->pid = process->pid;
    slot->address = address;
    slot->pages = pages;
    slot->memfd = memfd;
    slot->space = process->space;
    memfd->refs++;
    spin_unlock_irqrestore(&g_memfd_lock, lock);
    return address;
}

/* The mapping table is cleared under the lock; page-table teardown and the
 * object reference are released after unlocking because both take locks. */
static void mapping_destroy(struct memfd_mapping mapping)
{
    if (mapping.space) {
        for (u32 i = 0; i < mapping.pages; i++)
            vmm_space_unmap_page(mapping.space,
                                 mapping.address + (u64)i * PAGE_SIZE);
    }
    memfd_release(mapping.memfd);
}

int memfd_munmap(struct process *process, u64 address, u64 length)
{
    if (!process || length == 0)
        return -1;
    (void)length;

    struct memfd_mapping found;
    memset(&found, 0, sizeof(found));

    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    for (int i = 0; i < MEMFD_MAX_MAPPINGS; i++) {
        struct memfd_mapping *mapping = &g_mappings[i];
        if (!mapping->used || mapping->pid != process->pid ||
            mapping->address != address)
            continue;
        found = *mapping;
        memset(mapping, 0, sizeof(*mapping));
        break;
    }
    spin_unlock_irqrestore(&g_memfd_lock, lock);

    if (!found.used)
        return -1;
    mapping_destroy(found);
    return 0;
}

void memfd_process_exit(struct process *process)
{
    if (!process)
        return;

    struct memfd_mapping orphans[MEMFD_MAX_MAPPINGS];
    int count = 0;

    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    for (int i = 0; i < MEMFD_MAX_MAPPINGS; i++) {
        struct memfd_mapping *mapping = &g_mappings[i];
        if (!mapping->used || mapping->pid != process->pid)
            continue;
        orphans[count++] = *mapping;
        memset(mapping, 0, sizeof(*mapping));
    }
    spin_unlock_irqrestore(&g_memfd_lock, lock);

    for (int i = 0; i < count; i++)
        mapping_destroy(orphans[i]);
}

/* Trusted display service only: expose the physical page behind a memfd
 * mapping so virtio-gpu can attach it as scanout backing. */
u64 memfd_physical(struct memfd *memfd, u32 index)
{
    if (!memfd)
        return 0;
    u64 lock = spin_lock_irqsave(&g_memfd_lock);
    u64 physical = index < memfd->pages ? memfd->physical[index] : 0;
    spin_unlock_irqrestore(&g_memfd_lock, lock);
    return physical;
}

short memfd_poll(struct memfd *memfd, short events)
{
    if (!memfd)
        return ZPOLLNVAL;
    return (ZPOLLIN | ZPOLLOUT) & events;
}
