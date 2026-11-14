/*
 * Zhaoxu OS - the int 0x80 system call surface.
 *
 * Kept apart from process lifecycle and ELF loading (process.c) so each
 * file owns one responsibility.  user_range_ok/copy_user_string validate
 * every pointer against the calling process address space.
 */
#include "process_internal.h"
#include "io_syscall.h"

#include <boot.h>
#include <console.h>
#include <dev.h>
#include <drivers.h>
#include <fat32.h>
#include <install.h>
#include <ipc.h>
#include <kprintf.h>
#include <mem.h>
#include <memfd.h>
#include <rtc.h>
#include <shm.h>
#include <smp.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>

#define USER_ADDRESS_LIMIT  0x0000018000000000ULL

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2
#define SYS_OPEN   3
#define SYS_READ   4
#define SYS_CLOSE  5
#define SYS_UPTIME 6
#define SYS_SPAWN       7
#define SYS_WAIT        8
#define SYS_PORT_LISTEN 9
#define SYS_PORT_CONNECT 10
#define SYS_PORT_ACCEPT 11
#define SYS_PORT_SEND   12
#define SYS_PORT_RECV   13
#define SYS_PORT_CLOSE  14
#define SYS_SHM_CREATE  15
#define SYS_SHM_MAP     16
#define SYS_SHM_UNMAP   17
#define SYS_SHM_CLOSE   18
#define SYS_FB_MAP      19
#define SYS_MOUSE       20
#define SYS_KEY         21
#define SYS_SLEEP       22
#define SYS_CPUS        23
#define SYS_RTC         24
#define SYS_READ_PATH   25
#define SYS_PORT_TRY_ACCEPT 26
#define SYS_PORT_TRY_RECV   27
#define SYS_SERIAL_WRITE    28
#define SYS_READDIR         29
#define SYS_DISKS           30
#define SYS_INSTALL         31
#define SYS_WRITE_FILE      32
#define SYS_UNLINK          33
#define SYS_DATA_VOLUME     34
#define SYS_DEV_ALLOC       35
#define SYS_BOOT_FONT       36
#define SYS_SPAWN_ARG       37
#define SYS_BLK_COUNT       38
#define SYS_BLK_INFO        39
#define SYS_BLK_READ        40
#define SYS_BLK_WRITE       41
#define SYS_BLK_FLUSH       42
#define SYS_SOCKET          43
#define SYS_BIND            44
#define SYS_LISTEN          45
#define SYS_ACCEPT          46
#define SYS_CONNECT         47
#define SYS_SENDMSG         48
#define SYS_RECVMSG         49
#define SYS_POLL            50
#define SYS_MEMFD_CREATE    51
#define SYS_FTRUNCATE       52
#define SYS_LSEEK           53
#define SYS_MMAP            54
#define SYS_MUNMAP          55
#define SYS_FCNTL           56
#define SYS_SOCKETPAIR      57
#define SYS_BOOT_WALLPAPER  58
#define SYS_MEMFD_PHYS      59
#define SYS_MAP_PHYS        60

#define BLK_SECTOR_BYTES 512u
#define BLK_MAX_SECTORS  8u

#define FB_USER_BASE    0x0000010040000000ULL
#define FB_MAX_PAGES    8192

struct user_fb_info {
    u64 address;
    u64 width;
    u64 height;
    u64 stride;
    u64 format;
};

int user_range_ok(struct process *process, u64 pointer, u64 length)
{
    if (length == 0)
        return 1;
    if (!process || pointer == 0 || pointer >= USER_ADDRESS_LIMIT)
        return 0;
    u64 last = pointer + length - 1;
    if (last < pointer || last >= USER_ADDRESS_LIMIT)
        return 0;

    u64 page = pointer & ~0xFFFULL;
    u64 last_page = last & ~0xFFFULL;
    for (; page <= last_page; page += PAGE_SIZE) {
        if (!vmm_space_page_is_user(process->space, page))
            return 0;
    }
    return 1;
}

static int copy_user_string(struct process *process, char *destination,
                            usize capacity, u64 source)
{
    if (!capacity)
        return -1;
    for (usize i = 0; i < capacity; i++) {
        if (user_range_ok(process, source + i, 1) != 1)
            return -1;
        char character = *(const char *)(uintptr_t)(source + i);
        destination[i] = character;
        if (character == 0)
            return 0;
    }
    destination[capacity - 1] = 0;
    return -1;
}

static long syscall_write(struct process *process, u64 descriptor, u64 buffer,
                          u64 length)
{
    if (descriptor >= ZFD_BASE)
        return sys_ipc_write(process, descriptor, buffer, length);
    if (descriptor != 1 && descriptor != 2)
        return -1;
    if (length > 65536)
        length = 65536;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;
    console_write_n((const char *)(uintptr_t)buffer, length);
    return (long)length;
}

static long syscall_open(struct process *process, u64 path_pointer)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    return vfs_open(path);
}

static long syscall_read(struct process *process, u64 descriptor, u64 buffer,
                         u64 length)
{
    if (descriptor >= ZFD_BASE)
        return sys_ipc_read(process, descriptor, buffer, length);
    if (length > 0x100000)
        length = 0x100000;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;
    return vfs_read((int)descriptor, (void *)(uintptr_t)buffer, length);
}

static long syscall_spawn(struct process *process, u64 path_pointer)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    return (long)process_spawn(path, 0, NULL);
}

static long syscall_spawn_arg(struct process *process, u64 path_pointer,
                              u64 arg_pointer)
{
    char path[256];
    char argument[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    if (copy_user_string(process, argument, sizeof(argument), arg_pointer) != 0)
        return -1;
    char *argv[1] = {argument};
    return (long)process_spawn(path, 1, argv);
}

static long syscall_wait(struct process *process, u64 pid, u64 status_pointer)
{
    int status = 0;
    if (process_wait(pid, &status) != 0)
        return -1;
    if (status_pointer) {
        if (user_range_ok(process, status_pointer, sizeof(int)) != 1)
            return -1;
        if (process_user_write(process, status_pointer, &status, sizeof(int)) != 0)
            return -1;
    }
    return status;
}

static long syscall_port_try_accept(struct process *process, u64 port,
                                    u64 channel_pointer)
{
    if (user_range_ok(process, channel_pointer, sizeof(u64)) != 1)
        return -1;
    u64 channel = 0;
    int status = ipc_try_accept(port, &channel);
    if (status > 0 && process_user_write(process, channel_pointer, &channel,
                                 sizeof(channel)) != 0)
        return -1;
    return status;
}

static long syscall_port_try_recv(struct process *process, u64 channel,
                                  u64 type_pointer, u64 data_pointer,
                                  u64 capacity)
{
    if (capacity > IPC_MSG_MAX)
        capacity = IPC_MSG_MAX;
    if (capacity && user_range_ok(process, data_pointer, capacity) != 1)
        return -1;

    u64 type = 0;
    u8 data[IPC_MSG_MAX];
    int length = ipc_try_recv(channel, &type, data, (u32)capacity);
    if (length <= 0)
        return length;

    if (type_pointer) {
        if (user_range_ok(process, type_pointer, sizeof(u64)) != 1)
            return -1;
        if (process_user_write(process, type_pointer, &type, sizeof(u64)) != 0)
            return -1;
    }
    if (length && process_user_write(process, data_pointer, data, (u64)length) != 0)
        return -1;
    return length;
}

static long syscall_fb_map(struct process *process, u64 info_pointer)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot || !boot->framebuffer_base || !process->space)
        return -1;
    if (user_range_ok(process, info_pointer,
                      sizeof(struct user_fb_info)) != 1)
        return -1;

    u64 bytes = (u64)boot->pitch * boot->height;
    u64 pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0 || pages > FB_MAX_PAGES)
        return -1;

    for (u64 i = 0; i < pages; i++) {
        if (vmm_space_map_page(process->space,
                               FB_USER_BASE + i * PAGE_SIZE,
                               boot->framebuffer_base + i * PAGE_SIZE,
                               VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            for (u64 undo = 0; undo < i; undo++)
                vmm_space_unmap_page(process->space,
                                     FB_USER_BASE + undo * PAGE_SIZE);
            return -1;
        }
    }

    struct user_fb_info info;
    info.address = FB_USER_BASE;
    info.width = boot->width;
    info.height = boot->height;
    info.stride = boot->pitch / 4;
    info.format = boot->pixel_format;
    if (process_user_write(process, info_pointer, &info, sizeof(info)) != 0)
        return -1;
    return (long)FB_USER_BASE;
}

static long syscall_mouse(struct process *process, u64 state_pointer)
{
    if (user_range_ok(process, state_pointer, 3 * sizeof(u64)) != 1)
        return -1;
    u64 state[3];
    int ready = 0;
    dev_mouse_state(state, &ready);
    if (process_user_write(process, state_pointer, state, sizeof(state)) != 0)
        return -1;
    return ready;
}

static long syscall_serial_write(struct process *process, u64 buffer,
                                 u64 length)
{
    if (length > 65536)
        length = 65536;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;

    console_write_n((const char *)(uintptr_t)buffer, (usize)length);
    return (long)length;
}

static long syscall_read_path(struct process *process, u64 path_pointer,
                              u64 buffer_pointer, u64 capacity)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    if (capacity > 16u * 1024 * 1024)
        capacity = 16u * 1024 * 1024;
    if (capacity && user_range_ok(process, buffer_pointer, capacity) != 1)
        return -1;

    u64 read = 0;
    if (vfs_read_into(path, (u8 *)(uintptr_t)buffer_pointer, capacity,
                      &read) != 0)
        return -1;
    return (long)read;
}

struct readdir_state {
    char *dst;
    u64 capacity;
    u64 used;
};

static void readdir_emit(const char *name, u64 size, int is_dir, void *context)
{
    struct readdir_state *state = context;
    char line[160];
    int written = is_dir
                      ? ksnprintf(line, sizeof(line), "d %s\n", name)
                      : ksnprintf(line, sizeof(line), "f %s %llu\n", name,
                                  size);
    if (written <= 0)
        return;

    u64 length = (u64)written < sizeof(line) ? (u64)written
                                             : sizeof(line) - 1;
    if (state->used + length > state->capacity)
        return;
    memcpy(state->dst + state->used, line, (usize)length);
    state->used += length;
}

static long syscall_readdir(struct process *process, u64 path_pointer,
                            u64 buffer_pointer, u64 capacity)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    if (capacity == 0 || capacity > 65536)
        return -1;
    if (user_range_ok(process, buffer_pointer, capacity) != 1)
        return -1;

    struct readdir_state state = {(char *)(uintptr_t)buffer_pointer, capacity,
                                  0};
    if (vfs_list(path, readdir_emit, &state) != 0)
        return -1;
    return (long)state.used;
}

static long syscall_disks(struct process *process, u64 buffer_pointer,
                          u64 capacity)
{
    if (capacity == 0 || capacity > 65536)
        return -1;
    if (user_range_ok(process, buffer_pointer, capacity) != 1)
        return -1;

    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    char text[1024];
    int used = ksnprintf(text, sizeof(text), "Idx Port Size Label Role Model\n");

    for (int i = 0; i < count && used > 0 && (u64)used < sizeof(text); i++) {
        const char *role = disks[i].is_esp      ? "media"
                           : disks[i].is_data   ? "data"
                           : disks[i].is_target ? "target"
                                                : "other";
        usize remaining = sizeof(text) - (usize)used;
        int written = ksnprintf(text + used, remaining,
                                " %d %d %llu MiB %s %s %s\n", disks[i].disk,
                                disks[i].port,
                                (disks[i].sectors * 512) >> 20,
                                disks[i].label[0] ? disks[i].label : "-",
                                role, disks[i].model);
        if (written <= 0)
            break;
        used += (usize)written < remaining ? written : (int)remaining - 1;
    }

    if ((usize)used >= sizeof(text))
        used = (int)sizeof(text) - 1;
    if ((u64)used > capacity)
        used = (int)capacity;
    if (process_user_write(process, buffer_pointer, text, (usize)used) != 0)
        return -1;
    return used;
}

static long syscall_install(u64 target, u64 gpt)
{
    int destination = (int)target;
    if (destination < 0)
        destination = install_pick_target();
    if (destination < 0)
        return -3;
    if (install_run(destination, gpt != 0, NULL) != 0) {
        kprintf("[install] %s\n", install_last_error());
        return -4;
    }
    return 0;
}

static long syscall_write_file(struct process *process, u64 path_pointer,
                               u64 data_pointer, u64 size)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    /* /mnt, /tmp and /run are writable; no backend takes a file above
     * VFS_WRITE_MAX (the ext4 service buffer is exactly that big), so
     * reject before copying the payload into the kernel. */
    if (size > VFS_WRITE_MAX)
        return -1;
    if (size && user_range_ok(process, data_pointer, size) != 1)
        return -1;

    u8 *buffer = size ? kmalloc(size) : NULL;
    if (size && !buffer)
        return -1;
    if (size)
        memcpy(buffer, (const void *)(uintptr_t)data_pointer, (usize)size);

    int result = vfs_write_file(path, buffer, size);
    kfree(buffer);
    return result == 0 ? (long)size : -1;
}

static long syscall_unlink(struct process *process, u64 path_pointer)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    return vfs_unlink(path) == 0 ? 0 : -1;
}

/* 1 when a data/root volume is mounted at /mnt (ext4 or the FAT32
 * compatibility volume), 0 when /mnt falls back to the embedded mirror. */
static long syscall_data_volume(void)
{
    const char *label = vfs_data_label();
    return label && label[0] ? 1 : 0;
}

static long syscall_boot_font(struct process *process, u64 buffer_pointer,
                              u64 capacity)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot->font_phys || !boot->font_size)
        return -1;
    if (boot->font_size > capacity)
        return -1;
    if (user_range_ok(process, buffer_pointer, boot->font_size) != 1)
        return -1;
    memcpy((void *)(uintptr_t)buffer_pointer,
           (const void *)(uintptr_t)boot->font_phys, (usize)boot->font_size);
    return (long)boot->font_size;
}

static long syscall_boot_wallpaper(struct process *process, u64 buffer_pointer,
                                   u64 capacity)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot->wallpaper_phys || !boot->wallpaper_size)
        return -1;
    if (boot->wallpaper_size > capacity)
        return -1;
    if (user_range_ok(process, buffer_pointer, boot->wallpaper_size) != 1)
        return -1;
    memcpy((void *)(uintptr_t)buffer_pointer,
           (const void *)(uintptr_t)boot->wallpaper_phys,
           (usize)boot->wallpaper_size);
    return (long)boot->wallpaper_size;
}

/* Only the kernel-spawned display service may turn scanout memfd pages into
 * virtio-gpu backing addresses. */
static long syscall_memfd_phys(struct process *process, u64 fd, u64 index)
{
    if (!process || !process->display_cap)
        return -1;
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_MEMFD)
        return -1;
    u64 physical = memfd_physical((struct memfd *)entry->object, (u32)index);
    return physical ? (long)physical : -1;
}

static u64 g_display_map_va = 0x0000010200000000ULL; /* 1 TiB + 8 GiB */

/* Map a device MMIO BAR (possibly above 4 GiB) into the trusted display
 * service.  PCI BARs on modern virtio live above the 0..4 GiB identity map. */
static long syscall_map_phys(struct process *process, u64 physical, u64 size)
{
    if (!process || !process->space || !process->display_cap)
        return -1;
    if (!size || (physical & 0xFFF) || (size & 0xFFF))
        return -1;
    u64 pages = size / PAGE_SIZE;
    if (pages > 4096)
        return -1;
    u64 address = g_display_map_va;
    if (address + size > 0x0000018000000000ULL)
        return -1;
    g_display_map_va += size;

    for (u64 i = 0; i < pages; i++) {
        if (vmm_space_map_page(process->space, address + i * PAGE_SIZE,
                               physical + i * PAGE_SIZE,
                               VMM_PRESENT | VMM_WRITABLE | VMM_USER |
                                   VMM_EXECUTE_DISABLE) != 0) {
            for (u64 undo = 0; undo < i; undo++)
                vmm_space_unmap_page(process->space,
                                     address + undo * PAGE_SIZE);
            return -1;
        }
    }
    return (long)address;
}

static long syscall_rtc(struct process *process, u64 time_pointer)
{
    struct rtc_time now;
    if (user_range_ok(process, time_pointer, sizeof(now)) != 1)
        return -1;
    if (dev_rtc_read(&now) != 0)
        return -1;
    if (process_user_write(process, time_pointer, &now, sizeof(now)) != 0)
        return -1;
    return 0;
}

/* ---- trusted block I/O (only the ext4 service holds the capability) ----
 *
 * The payload always goes through a kernel scratch buffer: the driver RPC
 * can sleep while waiting for the busy lock, and a sleeping kernel thread may
 * be rescheduled onto another process's CR3, where a user pointer is no
 * longer valid. */
static long syscall_blk_count(struct process *process)
{
    return process->block_cap ? dev_disk_count() : -1;
}

static long syscall_blk_info(struct process *process, u64 disk,
                             u64 sectors_pointer, u64 port_pointer)
{
    if (!process->block_cap ||
        user_range_ok(process, sectors_pointer, sizeof(u64)) != 1 ||
        user_range_ok(process, port_pointer, sizeof(u64)) != 1)
        return -1;
    u64 sectors = dev_disk_sectors((int)disk);
    u64 port = (u64)(i64)dev_disk_port((int)disk);
    if (process_user_write(process, sectors_pointer, &sectors,
                           sizeof(u64)) != 0 ||
        process_user_write(process, port_pointer, &port, sizeof(u64)) != 0)
        return -1;
    return 0;
}

static int blk_user_bytes(struct process *process, u64 buffer, u64 count,
                          u8 **scratch)
{
    u64 bytes = 0;
    *scratch = NULL;
    if (count == 0 || count > BLK_MAX_SECTORS)
        return -1;
    bytes = count * BLK_SECTOR_BYTES;
    if (user_range_ok(process, buffer, bytes) != 1)
        return -1;
    *scratch = kmalloc(bytes);
    return *scratch ? 0 : -1;
}

static long syscall_blk_read(struct process *process, u64 disk, u64 lba,
                             u64 count, u64 buffer)
{
    if (!process->block_cap)
        return -1;
    u8 *scratch = NULL;
    if (blk_user_bytes(process, buffer, count, &scratch) != 0)
        return -1;

    long result = -1;
    if (dev_disk_read((int)disk, lba, (u32)count, scratch) == 0 &&
        process_user_write(process, buffer, scratch,
                           count * BLK_SECTOR_BYTES) == 0)
        result = (long)count;
    kfree(scratch);
    return result;
}

static long syscall_blk_write(struct process *process, u64 disk, u64 lba,
                              u64 count, u64 buffer)
{
    if (!process->block_cap)
        return -1;
    u8 *scratch = NULL;
    if (blk_user_bytes(process, buffer, count, &scratch) != 0)
        return -1;

    memcpy(scratch, (const void *)(uintptr_t)buffer,
           (usize)(count * BLK_SECTOR_BYTES));
    long result = dev_disk_write((int)disk, lba, (u32)count, scratch) == 0
                      ? (long)count
                      : -1;
    kfree(scratch);
    return result;
}

static long syscall_blk_flush(struct process *process, u64 disk)
{
    if (!process->block_cap)
        return -1;
    return dev_disk_flush((int)disk) == 0 ? 0 : -1;
}

static long syscall_port_listen(struct process *process, u64 name_pointer)
{
    char name[IPC_NAME_MAX];
    if (copy_user_string(process, name, sizeof(name), name_pointer) != 0)
        return -1;
    return (long)ipc_listen(name);
}

static long syscall_port_connect(struct process *process, u64 name_pointer)
{
    char name[IPC_NAME_MAX];
    if (copy_user_string(process, name, sizeof(name), name_pointer) != 0)
        return -1;
    return (long)ipc_connect(name);
}

static long syscall_port_send(struct process *process, u64 channel, u64 type,
                              u64 data_pointer, u64 length)
{
    if (length > IPC_MSG_MAX)
        return -1;
    if (length && user_range_ok(process, data_pointer, length) != 1)
        return -1;

    u8 data[IPC_MSG_MAX];
    if (length)
        memcpy(data, (const void *)(uintptr_t)data_pointer, length);
    return ipc_send(channel, type, data, (u32)length);
}

static long syscall_port_recv(struct process *process, u64 channel,
                              u64 type_pointer, u64 data_pointer, u64 capacity)
{
    if (capacity > IPC_MSG_MAX)
        capacity = IPC_MSG_MAX;
    if (capacity && user_range_ok(process, data_pointer, capacity) != 1)
        return -1;

    u64 type = 0;
    u8 data[IPC_MSG_MAX];
    int length = ipc_recv(channel, &type, data, (u32)capacity);
    if (length < 0)
        return -1;

    if (type_pointer) {
        if (user_range_ok(process, type_pointer, sizeof(u64)) != 1)
            return -1;
        if (process_user_write(process, type_pointer, &type, sizeof(u64)) != 0)
            return -1;
    }
    if (length) {
        if (process_user_write(process, data_pointer, data, (u64)length) != 0)
            return -1;
    }
    return length;
}

void syscall_dispatch(struct interrupt_frame *frame)
{
    struct process *process = process_current();
    if (!process) {
        frame->rax = (u64)-1;
        return;
    }

    long result = -1;
    switch (frame->rax) {
    case SYS_EXIT:
        process_exit_current((int)frame->rdi);
        return;
    case SYS_WRITE:
        result = syscall_write(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_GETPID:
        result = (long)process->pid;
        break;
    case SYS_OPEN:
        result = syscall_open(process, frame->rdi);
        break;
    case SYS_READ:
        result = syscall_read(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_CLOSE:
        result = frame->rdi >= ZFD_BASE ? sys_ipc_close(process, frame->rdi)
                                        : vfs_close((int)frame->rdi);
        break;
    case SYS_UPTIME:
        result = (long)(timer_ticks() * 1000 / timer_hz());
        break;
    case SYS_SPAWN:
        result = syscall_spawn(process, frame->rdi);
        break;
    case SYS_WAIT:
        result = syscall_wait(process, frame->rdi, frame->rsi);
        break;
    case SYS_PORT_LISTEN:
        result = syscall_port_listen(process, frame->rdi);
        break;
    case SYS_PORT_CONNECT:
        result = syscall_port_connect(process, frame->rdi);
        break;
    case SYS_PORT_ACCEPT:
        result = (long)ipc_accept(frame->rdi);
        break;
    case SYS_PORT_SEND:
        result = syscall_port_send(process, frame->rdi, frame->rsi,
                                   frame->rdx, frame->r10);
        break;
    case SYS_PORT_RECV:
        result = syscall_port_recv(process, frame->rdi, frame->rsi,
                                   frame->rdx, frame->r10);
        break;
    case SYS_PORT_CLOSE:
        result = ipc_close(frame->rdi);
        break;
    case SYS_SHM_CREATE:
        result = (long)shm_create(frame->rdi);
        break;
    case SYS_SHM_MAP:
        result = (long)shm_map(frame->rdi);
        break;
    case SYS_SHM_UNMAP:
        result = shm_unmap(frame->rdi);
        break;
    case SYS_SHM_CLOSE:
        result = shm_close(frame->rdi);
        break;
    case SYS_FB_MAP:
        result = syscall_fb_map(process, frame->rdi);
        break;
    case SYS_MOUSE:
        result = syscall_mouse(process, frame->rdi);
        break;
    case SYS_KEY:
        result = console_read_char();
        break;
    case SYS_SLEEP:
        thread_sleep_ms((u32)frame->rdi);
        result = 0;
        break;
    case SYS_CPUS:
        result = (long)smp_cpu_count();
        break;
    case SYS_RTC:
        result = syscall_rtc(process, frame->rdi);
        break;
    case SYS_READ_PATH:
        result = syscall_read_path(process, frame->rdi, frame->rsi,
                                   frame->rdx);
        break;
    case SYS_PORT_TRY_ACCEPT:
        result = syscall_port_try_accept(process, frame->rdi, frame->rsi);
        break;
    case SYS_PORT_TRY_RECV:
        result = syscall_port_try_recv(process, frame->rdi, frame->rsi,
                                       frame->rdx, frame->r10);
        break;
    case SYS_SERIAL_WRITE:
        result = syscall_serial_write(process, frame->rdi, frame->rsi);
        break;
    case SYS_READDIR:
        result = syscall_readdir(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_DISKS:
        result = syscall_disks(process, frame->rdi, frame->rsi);
        break;
    case SYS_INSTALL:
        result = syscall_install(frame->rdi, frame->rsi);
        break;
    case SYS_WRITE_FILE:
        result = syscall_write_file(process, frame->rdi, frame->rsi,
                                    frame->rdx);
        break;
    case SYS_UNLINK:
        result = syscall_unlink(process, frame->rdi);
        break;
    case SYS_DATA_VOLUME:
        result = syscall_data_volume();
        break;
    case SYS_DEV_ALLOC:
        result = (long)dev_alloc_page();
        break;
    case SYS_BOOT_FONT:
        result = syscall_boot_font(process, frame->rdi, frame->rsi);
        break;
    case SYS_SPAWN_ARG:
        result = syscall_spawn_arg(process, frame->rdi, frame->rsi);
        break;
    case SYS_BLK_COUNT:
        result = syscall_blk_count(process);
        break;
    case SYS_BLK_INFO:
        result = syscall_blk_info(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_BLK_READ:
        result = syscall_blk_read(process, frame->rdi, frame->rsi, frame->rdx,
                                  frame->r10);
        break;
    case SYS_BLK_WRITE:
        result = syscall_blk_write(process, frame->rdi, frame->rsi,
                                   frame->rdx, frame->r10);
        break;
    case SYS_BLK_FLUSH:
        result = syscall_blk_flush(process, frame->rdi);
        break;
    case SYS_SOCKET:
        result = sys_ipc_socket(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_BIND:
        result = sys_ipc_bind(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_LISTEN:
        result = sys_ipc_listen(process, frame->rdi, frame->rsi);
        break;
    case SYS_ACCEPT:
        result = sys_ipc_accept(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_CONNECT:
        result = sys_ipc_connect(process, frame->rdi, frame->rsi,
                                 frame->rdx);
        break;
    case SYS_SENDMSG:
        result = sys_ipc_sendmsg(process, frame->rdi, frame->rsi,
                                 frame->rdx);
        break;
    case SYS_RECVMSG:
        result = sys_ipc_recvmsg(process, frame->rdi, frame->rsi,
                                 frame->rdx);
        break;
    case SYS_POLL:
        result = sys_ipc_poll(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_MEMFD_CREATE:
        result = sys_ipc_memfd_create(process, frame->rdi, frame->rsi);
        break;
    case SYS_FTRUNCATE:
        result = sys_ipc_ftruncate(process, frame->rdi, frame->rsi);
        break;
    case SYS_LSEEK:
        result = sys_ipc_lseek(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_MMAP:
        result = sys_ipc_mmap(process, frame->rdi);
        break;
    case SYS_MUNMAP:
        result = sys_ipc_munmap(process, frame->rdi, frame->rsi);
        break;
    case SYS_FCNTL:
        result = sys_ipc_fcntl(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_SOCKETPAIR:
        result = sys_ipc_socketpair(process, frame->rdi, frame->rsi,
                                    frame->rdx, frame->r10);
        break;
    case SYS_BOOT_WALLPAPER:
        result = syscall_boot_wallpaper(process, frame->rdi, frame->rsi);
        break;
    case SYS_MEMFD_PHYS:
        result = syscall_memfd_phys(process, frame->rdi, frame->rsi);
        break;
    case SYS_MAP_PHYS:
        result = syscall_map_phys(process, frame->rdi, frame->rsi);
        break;
    default:
        result = -1;
        break;
    }
    frame->rax = (u64)result;
}

