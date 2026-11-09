/*
 * Zhaoxu OS - shared memory objects.
 *
 * A creator owns one reference; every map adds one.  ADE maps a client's
 * command ring read-only by convention, but the kernel only guarantees
 * shared physical pages -- the protocol defines access rules.
 */
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <shm.h>
#include <spinlock.h>
#include <string.h>
#include <types.h>

#define SHM_VA_BASE 0x0000010020000000ULL   /* 1 TiB + 8 GiB */
#define SHM_MAX_ATTACH 32

struct shm_object {
    int used;
    u64 id;
    u64 owner;
    u32 pages;
    u32 refs;
    u64 physical[SHM_MAX_PAGES];
};

struct shm_attach {
    int used;
    u64 pid;
    u64 id;
    u64 address;
    u32 pages;
};

static struct shm_object g_objects[SHM_MAX_OBJECTS];
static struct shm_attach g_attaches[SHM_MAX_ATTACH];
static spinlock_t g_shm_lock = SPINLOCK_INIT;
static u64 g_next_id = 1;
static u64 g_next_va = SHM_VA_BASE;

static struct shm_object *object_find(u64 id)
{
    for (int i = 0; i < SHM_MAX_OBJECTS; i++) {
        if (g_objects[i].used && g_objects[i].id == id)
            return &g_objects[i];
    }
    return NULL;
}

static void object_free(struct shm_object *object)
{
    for (u32 i = 0; i < object->pages; i++) {
        if (object->physical[i])
            pmm_free_page(object->physical[i]);
    }
    memset(object, 0, sizeof(*object));
}

static void object_put(struct shm_object *object)
{
    if (object->refs > 0)
        object->refs--;
    if (object->refs == 0)
        object_free(object);
}

u64 shm_create(u64 size)
{
    if (size == 0)
        return 0;
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > SHM_MAX_PAGES)
        return 0;
    u64 pid = process_self_pid();
    if (!pid)
        return 0;

    struct shm_object *slot = NULL;
    u64 flags = spin_lock_irqsave(&g_shm_lock);
    for (int i = 0; i < SHM_MAX_OBJECTS; i++) {
        if (!g_objects[i].used) {
            slot = &g_objects[i];
            break;
        }
    }
    if (!slot) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = g_next_id++;
    slot->owner = pid;
    slot->pages = (u32)pages;
    slot->refs = 1;

    for (u32 i = 0; i < pages; i++) {
        u64 physical = pmm_alloc_page();
        if (!physical) {
            slot->pages = i;
            object_free(slot);
            spin_unlock_irqrestore(&g_shm_lock, flags);
            return 0;
        }
        memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
        slot->physical[i] = physical;
    }

    u64 id = slot->id;
    spin_unlock_irqrestore(&g_shm_lock, flags);
    return id;
}

u64 shm_map(u64 id)
{
    u64 pid = process_self_pid();
    if (!pid)
        return 0;
    struct address_space *space = process_current_space();
    if (!space)
        return 0;

    u64 flags = spin_lock_irqsave(&g_shm_lock);

    struct shm_object *object = object_find(id);
    if (!object) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return 0;
    }

    static struct shm_attach *slot;
    slot = NULL;
    for (int i = 0; i < SHM_MAX_ATTACH; i++) {
        if (!g_attaches[i].used) {
            slot = &g_attaches[i];
            break;
        }
    }
    if (!slot) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return 0;
    }

    u64 address = g_next_va;
    g_next_va += (u64)object->pages * PAGE_SIZE;
    if (g_next_va >= 0x0000018000000000ULL) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return 0;
    }

    for (u32 i = 0; i < object->pages; i++) {
        if (vmm_space_map_page(space, address + (u64)i * PAGE_SIZE,
                               object->physical[i],
                               VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            for (u32 undo = 0; undo < i; undo++)
                vmm_space_unmap_page(space,
                                     address + (u64)undo * PAGE_SIZE);
            spin_unlock_irqrestore(&g_shm_lock, flags);
            return 0;
        }
    }

    slot->used = 1;
    slot->pid = pid;
    slot->id = id;
    slot->address = address;
    slot->pages = object->pages;
    object->refs++;

    spin_unlock_irqrestore(&g_shm_lock, flags);
    return address;
}

int shm_unmap(u64 id)
{
    u64 pid = process_self_pid();
    if (!pid)
        return -1;
    struct address_space *space = process_current_space();
    if (!space)
        return -1;

    u64 flags = spin_lock_irqsave(&g_shm_lock);

    struct shm_attach *attach = NULL;
    for (int i = 0; i < SHM_MAX_ATTACH; i++) {
        if (g_attaches[i].used && g_attaches[i].pid == pid &&
            g_attaches[i].id == id) {
            attach = &g_attaches[i];
            break;
        }
    }
    if (!attach) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return -1;
    }

    for (u32 i = 0; i < attach->pages; i++)
        vmm_space_unmap_page(space, attach->address + (u64)i * PAGE_SIZE);

    struct shm_object *object = object_find(id);
    attach->used = 0;
    if (object)
        object_put(object);

    spin_unlock_irqrestore(&g_shm_lock, flags);
    return 0;
}

int shm_close(u64 id)
{
    u64 pid = process_self_pid();
    if (!pid)
        return -1;

    u64 flags = spin_lock_irqsave(&g_shm_lock);
    struct shm_object *object = object_find(id);
    if (!object || object->owner != pid) {
        spin_unlock_irqrestore(&g_shm_lock, flags);
        return -1;
    }
    object->owner = 0;
    object_put(object);
    spin_unlock_irqrestore(&g_shm_lock, flags);
    return 0;
}

void shm_process_exit(u64 pid)
{
    if (!pid)
        return;
    struct address_space *space = process_current_space();

    u64 flags = spin_lock_irqsave(&g_shm_lock);

    for (int i = 0; i < SHM_MAX_ATTACH; i++) {
        struct shm_attach *attach = &g_attaches[i];
        if (!attach->used || attach->pid != pid)
            continue;

        if (space) {
            for (u32 page = 0; page < attach->pages; page++)
                vmm_space_unmap_page(space,
                                     attach->address +
                                         (u64)page * PAGE_SIZE);
        }

        struct shm_object *object = object_find(attach->id);
        attach->used = 0;
        if (object)
            object_put(object);
    }

    for (int i = 0; i < SHM_MAX_OBJECTS; i++) {
        struct shm_object *object = &g_objects[i];
        if (object->used && object->owner == pid) {
            object->owner = 0;
            object_put(object);
        }
    }

    spin_unlock_irqrestore(&g_shm_lock, flags);
}
