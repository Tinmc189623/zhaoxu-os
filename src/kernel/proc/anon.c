/*
 * Zhaoxu OS - per-process anonymous mappings.
 *
 * glibc's malloc and TLS need MAP_ANONYMOUS at a rate the memfd pool cannot
 * sustain (64 objects system-wide, a monotonic 1.5 GiB window, offset 0 and
 * fixed RW/NX).  This allocator serves that range from a dedicated 48 GiB VA
 * window, tracks regions per process, and hands every page to the ordinary
 * process_map_page/vmm_space_* machinery so fork and teardown stay uniform.
 */
#include <mem.h>
#include <string.h>
#include <types.h>

#include "process_internal.h"

#define ANON_VA_BASE  0x0000010400000000ULL   /* 1 TiB + 16 GiB */
#define ANON_VA_LIMIT 0x0000011000000000ULL   /* 1 TiB + 64 GiB */
#define ANON_MAX_BYTES (512ULL * 1024 * 1024)

#define ANON_PROT_WRITE 0x2u
#define ANON_PROT_EXEC  0x4u
#define ANON_MAP_FIXED  0x10u

#define ANON_ENOMEM (-12L)
#define ANON_EINVAL (-22L)

#define ANON_MADV_DONTNEED 4
#define ANON_MADV_FREE     8

static u64 page_up(u64 value)
{
    return ALIGN_UP(value, PAGE_SIZE);
}

static u64 page_down(u64 value)
{
    return value & ~(u64)(PAGE_SIZE - 1);
}

static u64 prot_flags(u32 prot)
{
    u64 flags = 0;
    if (prot & ANON_PROT_WRITE)
        flags |= VMM_WRITABLE;
    if (!(prot & ANON_PROT_EXEC))
        flags |= VMM_EXECUTE_DISABLE;
    return flags;
}

/* A range is free when it lies inside the window and touches no region. */
static int range_free(const struct process *process, u64 start, u64 end)
{
    if (start < ANON_VA_BASE || end > ANON_VA_LIMIT || start >= end)
        return 0;
    for (int i = 0; i < process->anon_count; i++) {
        if (start < process->anon_regions[i].end &&
            process->anon_regions[i].start < end)
            return 0;
    }
    return 1;
}

/* Return the first gap at or above `wanted`; 0 when the window is full. */
static u64 first_fit(struct process *process, u64 wanted, u64 length)
{
    u64 cursor = wanted < ANON_VA_BASE ? ANON_VA_BASE : page_up(wanted);
    for (int i = 0; i < process->anon_count; i++) {
        const struct anon_region *region = &process->anon_regions[i];
        if (cursor + length <= region->start)
            break;
        if (region->end > cursor)
            cursor = page_up(region->end);
    }
    return range_free(process, cursor, cursor + length) ? cursor : 0;
}

/* Insert without merging; regions stay sorted by start. */
static int region_insert(struct process *process, u64 start, u64 end, u32 prot)
{
    if (process->anon_count == ANON_MAX_REGIONS)
        return -1;
    int index = process->anon_count;
    while (index > 0 && process->anon_regions[index - 1].start > start) {
        process->anon_regions[index] = process->anon_regions[index - 1];
        index--;
    }
    process->anon_regions[index].start = start;
    process->anon_regions[index].end = end;
    process->anon_regions[index].prot = prot;
    process->anon_regions[index].pad = 0;
    process->anon_count++;

    /* Merge touching neighbours with the same protection. */
    for (int i = process->anon_count - 1; i > 0; i--) {
        struct anon_region *left = &process->anon_regions[i - 1];
        struct anon_region *right = &process->anon_regions[i];
        if (left->end == right->start && left->prot == right->prot) {
            left->end = right->end;
            process->anon_regions[i] = process->anon_regions[--process->anon_count];
            break;
        }
    }
    return 0;
}

/* Remove [start,end) from the region list, splitting a straddling region. */
static int regions_remove(struct process *process, u64 start, u64 end)
{
    int touched = 0;
    for (int i = 0; i < process->anon_count;) {
        struct anon_region *region = &process->anon_regions[i];
        if (region->end <= start || region->start >= end) {
            i++;
            continue;
        }
        touched = 1;
        if (region->start < start && region->end > end) {
            if (process->anon_count == ANON_MAX_REGIONS)
                return touched;
            u64 old_end = region->end;
            u32 old_prot = region->prot;
            region->end = start;
            for (int j = process->anon_count; j > i + 1; j--)
                process->anon_regions[j] = process->anon_regions[j - 1];
            struct anon_region *tail = &process->anon_regions[i + 1];
            tail->start = end;
            tail->end = old_end;
            tail->prot = old_prot;
            tail->pad = 0;
            process->anon_count++;
            i += 2;
            continue;
        }
        if (region->start < start)
            region->end = start;
        else if (region->end > end) {
            region->start = end;
            i++;
        } else {
            process->anon_regions[i] =
                process->anon_regions[--process->anon_count];
        }
    }
    return touched;
}

long process_anon_mmap(struct process *process, u64 address, u64 length,
                       u32 prot, u32 flags)
{
    if (!process || !process->space || length == 0 || length > ANON_MAX_BYTES)
        return ANON_EINVAL;
    u64 rounded = page_up(length);
    if (rounded < length || rounded > ANON_MAX_BYTES)
        return ANON_EINVAL;

    u64 start;
    if (flags & ANON_MAP_FIXED) {
        start = page_down(address);
        if ((address & (PAGE_SIZE - 1)) || start < ANON_VA_BASE ||
            start + rounded > ANON_VA_LIMIT)
            return ANON_EINVAL;
        process_anon_munmap(process, start, rounded);
    } else {
        u64 wanted = address ? page_down(address) : ANON_VA_BASE;
        start = address ? first_fit(process, wanted, rounded) : 0;
        if (!start)
            start = first_fit(process, ANON_VA_BASE, rounded);
        if (!start)
            return ANON_ENOMEM;
    }

    u64 mapped = start;
    for (; mapped < start + rounded; mapped += PAGE_SIZE) {
        if (!process_map_page(process, mapped, prot_flags(prot)))
            break;
    }
    if (mapped < start + rounded) {
        for (u64 undo = start; undo < mapped; undo += PAGE_SIZE)
            process_unmap_page(process, undo);
        return ANON_ENOMEM;
    }
    if (region_insert(process, start, start + rounded, prot) != 0) {
        for (u64 undo = start; undo < start + rounded; undo += PAGE_SIZE)
            process_unmap_page(process, undo);
        return ANON_ENOMEM;
    }
    if (start + rounded > process->anon_cursor)
        process->anon_cursor = start + rounded;
    return (long)start;
}

/* Unmap [address,address+length).  Ranges outside the anonymous window are
 * left to the memfd path; inside it the call always succeeds. */
long process_anon_munmap(struct process *process, u64 address, u64 length)
{
    if (!process || length == 0)
        return ANON_EINVAL;
    u64 start = page_down(address);
    u64 end = page_up(address + length);
    if (end <= start || end <= ANON_VA_BASE || start >= ANON_VA_LIMIT)
        return ANON_EINVAL;

    regions_remove(process, start, end);
    for (u64 va = start; va < end; va += PAGE_SIZE)
        process_unmap_page(process, va);
    return 0;
}

/* Change protection over [address,address+length).  The range must already be
 * mapped; a straddling region is split into the untouched outer pieces (old
 * protection) and one entry for the middle. */
long process_anon_mprotect(struct process *process, u64 address, u64 length,
                           u32 prot)
{
    if (!process || !process->space || length == 0)
        return ANON_EINVAL;
    u64 start = page_down(address);
    u64 end = page_up(address + length);
    if (end <= start)
        return ANON_EINVAL;

    u64 covered = start;
    int extra = 0;
    for (int i = 0; i < process->anon_count; i++) {
        const struct anon_region *region = &process->anon_regions[i];
        if (region->end <= covered) {
            if (region->start >= end)
                break;
            continue;
        }
        if (region->start > covered)
            break;
        covered = region->end;
        if (region->start < start && region->end > end)
            extra++;
        if (covered >= end)
            break;
    }
    if (covered < end)
        return ANON_ENOMEM;
    if (process->anon_count + extra + 1 > ANON_MAX_REGIONS)
        return ANON_ENOMEM;

    for (int i = 0; i < process->anon_count;) {
        struct anon_region *region = &process->anon_regions[i];
        if (start >= region->end || end <= region->start) {
            i++;
            continue;
        }
        if (region->start >= start && region->end <= end) {
            region->prot = prot;
            i++;
            continue;
        }
        u64 old_end = region->end;
        u32 old_prot = region->prot;
        if (region->start < start) {
            region->end = start;
            if (old_end > end)
                region_insert(process, end, old_end, old_prot);
            i++;
        } else {
            region->start = end;
            i++;
        }
    }
    if (region_insert(process, start, end, prot) != 0)
        return ANON_ENOMEM;
    for (u64 va = start; va < end; va += PAGE_SIZE) {
        if (vmm_space_protect_page(process->space, va, prot_flags(prot)) != 0)
            return ANON_ENOMEM;
    }
    return 0;
}

long process_anon_madvise(struct process *process, u64 address, u64 length,
                          int advice)
{
    if (!process || !process->space || length == 0)
        return ANON_EINVAL;
    if (advice != ANON_MADV_DONTNEED && advice != ANON_MADV_FREE)
        return 0;
    u64 start = page_down(address);
    u64 end = page_up(address + length);
    for (u64 va = start; va < end; va += PAGE_SIZE) {
        u64 physical = vmm_space_get_physical(process->space, va);
        if (physical)
            memset((void *)(usize)physical, 0, PAGE_SIZE);
    }
    return 0;
}
