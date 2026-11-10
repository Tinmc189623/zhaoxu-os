/*
 * Zhaoxu OS - the int 0x80 system call surface.
 *
 * Kept apart from process lifecycle and ELF loading (process.c) so each
 * file owns one responsibility.  user_range_ok/copy_user_string validate
 * every pointer against the calling process address space.
 */
#include "process_internal.h"

#include <boot.h>
#include <console.h>
#include <dev.h>
#include <drivers.h>
#include <fat32.h>
#include <install.h>
#include <ipc.h>
#include <kprintf.h>
#include <mem.h>
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

#define FB_USER_BASE    0x0000010040000000ULL
#define FB_MAX_PAGES    8192

struct user_fb_info {
    u64 address;
    u64 width;
    u64 height;
    u64 stride;
    u64 format;
};

static int user_range_ok(struct process *process, u64 pointer, u64 length)
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
    return install_run(destination, gpt != 0, NULL) == 0 ? 0 : -4;
}

static long syscall_write_file(struct process *process, u64 path_pointer,
                               u64 data_pointer, u64 size)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    if (size > FAT32_MAX_FILE_SIZE)
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

static long syscall_data_volume(void)
{
    const char *label = vfs_fat_label();
    return label && strcmp(label, "ZHAOXUDATA") == 0 ? 1 : 0;
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
        result = vfs_close((int)frame->rdi);
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
    default:
        result = -1;
        break;
    }
    frame->rax = (u64)result;
}

