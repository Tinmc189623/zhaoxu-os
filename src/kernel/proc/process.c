/*
 * Zhaoxu OS - user processes
 *
 * A single ring 3 process runs at a time.  The kernel keeps its higher-half
 * mappings and adds user pages in the low canonical range, so no address
 * space switch is required: the process shares the kernel page tables.
 */
#include <console.h>
#include <gdt.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <string.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>

#define USER_LOAD_BASE      0x0000000100000000ULL   /* 4 GiB   */
#define USER_STACK_TOP      0x0000000200000000ULL   /* 8 GiB   */
#define USER_STACK_PAGES    32
#define USER_MAX_PAGES      1024
#define USER_KERNEL_STACK   (16 * 1024)
#define USER_ADDRESS_LIMIT  0x0000800000000000ULL
#define BOOT_STACK_TOP      0xffffffff80f00000ULL

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2
#define SYS_OPEN   3
#define SYS_READ   4
#define SYS_CLOSE  5
#define SYS_UPTIME 6

struct user_mapping {
    u64 virtual_address;
    u64 physical_address;
};

static struct user_mapping g_mappings[USER_MAX_PAGES];
static int g_mapping_count;
static int g_active;
static u64 g_pid;
static int g_exit_code;
static u64 g_entry;
static u64 g_kernel_stack;

extern u64 g_kernel_rsp;
extern void user_enter(u64 entry, u64 stack);
extern void user_resume(void) __attribute__((noreturn));

struct elf64_header {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 program_header_offset;
    u64 section_header_offset;
    u32 flags;
    u16 header_size;
    u16 program_header_size;
    u16 program_header_count;
    u16 section_header_size;
    u16 section_header_count;
    u16 string_table_index;
} __attribute__((packed));

struct elf64_program_header {
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
} __attribute__((packed));

#define ELF_PT_LOAD 1

struct zxe_header {
    u8 magic[4];
    u16 version;
    u16 machine;
    u16 seg_count;
    u16 hdr_size;
    u32 reserved;
    u64 entry;
    u64 file_size;
} __attribute__((packed));

struct zxe_segment {
    u64 vaddr;
    u64 file_off;
    u64 file_size;
    u64 mem_size;
    u64 flags;
} __attribute__((packed));

#define ZXE_SEG_MAX 16

static u64 map_user_page(u64 virtual_address)
{
    for (int i = 0; i < g_mapping_count; i++) {
        if (g_mappings[i].virtual_address == virtual_address)
            return g_mappings[i].physical_address;
    }
    if (g_mapping_count >= USER_MAX_PAGES)
        return 0;

    u64 physical = pmm_alloc_page();
    if (!physical)
        return 0;
    if (vmm_map_page(virtual_address, physical,
                     VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        pmm_free_page(physical);
        return 0;
    }
    g_mappings[g_mapping_count].virtual_address = virtual_address;
    g_mappings[g_mapping_count].physical_address = physical;
    g_mapping_count++;
    return physical;
}

static void release_user_pages(void)
{
    for (int i = 0; i < g_mapping_count; i++) {
        u64 physical = vmm_unmap_page(g_mappings[i].virtual_address);
        if (physical)
            pmm_free_page(physical);
    }
    g_mapping_count = 0;
}

static int elf_load(const u8 *image, u64 image_size, u64 *entry_out)
{
    if (!image || image_size < sizeof(struct elf64_header))
        return -1;

    const struct elf64_header *header = (const struct elf64_header *)image;
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F')
        return -1;
    if (header->ident[4] != 2 || header->ident[5] != 1)
        return -1;
    if (header->type != 2 || header->machine != 62)
        return -1;
    if (header->program_header_size < sizeof(struct elf64_program_header))
        return -1;
    if (header->program_header_count == 0 ||
        header->program_header_count > 32)
        return -1;

    u64 table_end = header->program_header_offset +
                    (u64)header->program_header_count *
                        header->program_header_size;
    if (table_end > image_size)
        return -1;

    for (u32 i = 0; i < header->program_header_count; i++) {
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)
                (image + header->program_header_offset +
                 (u64)i * header->program_header_size);
        if (program->type != ELF_PT_LOAD || program->memory_size == 0)
            continue;
        if (program->file_size > program->memory_size)
            return -1;
        if (program->offset + program->file_size > image_size)
            return -1;
        if (program->virtual_address < USER_LOAD_BASE)
            return -1;
        if (program->virtual_address + program->memory_size <
            program->virtual_address)
            return -1;
        if (program->virtual_address + program->memory_size >
            USER_ADDRESS_LIMIT)
            return -1;

        u64 start = program->virtual_address & ~0xFFFULL;
        u64 end = (program->virtual_address + program->memory_size +
                   0xFFFULL) & ~0xFFFULL;

        for (u64 page = start; page < end; page += PAGE_SIZE) {
            if (!map_user_page(page))
                return -1;
        }

        if (program->file_size)
            memcpy((void *)(uintptr_t)program->virtual_address,
                   image + program->offset, program->file_size);
        if (program->memory_size > program->file_size)
            memset((void *)(uintptr_t)(program->virtual_address +
                                       program->file_size),
                   0, program->memory_size - program->file_size);
    }

    if (header->entry < USER_LOAD_BASE ||
        header->entry >= USER_ADDRESS_LIMIT)
        return -1;

    *entry_out = header->entry;
    return 0;
}

static int zxe_load(const u8 *image, u64 image_size, u64 *entry_out)
{
    if (!image || image_size < sizeof(struct zxe_header))
        return -1;

    const struct zxe_header *header = (const struct zxe_header *)image;
    if (header->magic[0] != 'Z' || header->magic[1] != 'X' ||
        header->magic[2] != 'E' || header->magic[3] != 1)
        return -1;
    if (header->version != 1 || header->machine != 62)
        return -1;
    if (header->seg_count == 0 || header->seg_count > ZXE_SEG_MAX)
        return -1;

    u64 expected_hdr = 32 + (u64)header->seg_count *
                             sizeof(struct zxe_segment);
    expected_hdr = (expected_hdr + 15) & ~15ULL;
    if (header->hdr_size != expected_hdr || header->hdr_size > image_size)
        return -1;
    if (header->file_size != image_size)
        return -1;

    for (u16 i = 0; i < header->seg_count; i++) {
        const struct zxe_segment *segment = (const struct zxe_segment *)
            (image + 32 + (u64)i * sizeof(struct zxe_segment));
        if (segment->vaddr < USER_LOAD_BASE)
            return -1;
        if (segment->vaddr & 0xFFF)
            return -1;
        if (segment->mem_size == 0 || segment->file_size > segment->mem_size)
            return -1;
        if (segment->vaddr + segment->mem_size < segment->vaddr)
            return -1;
        if (segment->vaddr + segment->mem_size > USER_ADDRESS_LIMIT)
            return -1;
        if (segment->file_off < header->hdr_size ||
            segment->file_off + segment->file_size > image_size ||
            segment->file_off + segment->file_size < segment->file_off)
            return -1;

        u64 end = (segment->vaddr + segment->mem_size + 0xFFFULL) & ~0xFFFULL;
        for (u64 page = segment->vaddr; page < end; page += PAGE_SIZE) {
            if (!map_user_page(page))
                return -1;
        }

        if (segment->file_size)
            memcpy((void *)(uintptr_t)segment->vaddr,
                   image + segment->file_off, segment->file_size);
        if (segment->mem_size > segment->file_size)
            memset((void *)(uintptr_t)(segment->vaddr +
                                       segment->file_size),
                   0, segment->mem_size - segment->file_size);
    }

    if (header->entry < USER_LOAD_BASE ||
        header->entry >= USER_ADDRESS_LIMIT)
        return -1;

    *entry_out = header->entry;
    return 0;
}

static int image_load(const u8 *image, u64 image_size, u64 *entry_out)
{
    if (image_size >= 4 && image[0] == 'Z' && image[1] == 'X' &&
        image[2] == 'E' && image[3] == 1)
        return zxe_load(image, image_size, entry_out);
    return elf_load(image, image_size, entry_out);
}

static u64 build_user_stack(int argc, const char **argv)
{
    u64 stack = USER_STACK_TOP - 16;
    u64 pointers[16];

    if (argc > 16)
        argc = 16;
    if (argc <= 0)
        return 0;

    for (int i = argc - 1; i >= 0; i--) {
        usize length = strlen(argv[i]) + 1;
        stack -= length;
        memcpy((void *)(uintptr_t)stack, argv[i], length);
        pointers[i] = stack;
    }

    stack &= ~0xFULL;
    stack -= (u64)(argc + 2) * sizeof(u64);
    u64 *frame = (u64 *)(uintptr_t)stack;
    frame[0] = (u64)argc;
    for (int i = 0; i < argc; i++)
        frame[1 + i] = pointers[i];
    frame[1 + argc] = 0;
    return stack;
}

void process_init(void)
{
    g_mapping_count = 0;
    g_active = 0;
    g_pid = 0;
    g_entry = 0;
    g_kernel_stack = 0;
    g_exit_code = 0;
    tss_set_rsp0(BOOT_STACK_TOP);
}

int process_run(const char *path, int argc, char **argv)
{
    if (g_active || !path)
        return -1;

    u8 *image = NULL;
    u64 image_size = 0;
    if (vfs_read_all(path, &image, &image_size) != 0) {
        kprintf("[proc] cannot read %s\n", path);
        return -1;
    }

    g_mapping_count = 0;
    u64 entry = 0;
    int status = image_load(image, image_size, &entry);
    kfree(image);
    if (status != 0) {
        kprintf("[proc] %s is not a valid ZXE/ELF64 executable\n", path);
        release_user_pages();
        return -1;
    }

    u64 stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    for (u64 page = stack_base; page < USER_STACK_TOP; page += PAGE_SIZE) {
        if (!map_user_page(page)) {
            release_user_pages();
            return -1;
        }
    }

    u8 *kernel_stack = kmalloc(USER_KERNEL_STACK);
    if (!kernel_stack) {
        release_user_pages();
        return -1;
    }

    const char *default_argv[1] = { path };
    const char **stack_argv = (argc > 0) ? (const char **)argv
                                         : default_argv;
    if (argc <= 0)
        argc = 1;
    u64 user_stack = build_user_stack(argc, stack_argv);

    g_active = 1;
    g_pid++;
    g_entry = entry;
    g_kernel_stack = (u64)(uintptr_t)kernel_stack + USER_KERNEL_STACK;
    g_exit_code = -1;

    u64 kernel_stack_top = g_kernel_stack & ~0xFULL;
    tss_set_rsp0(kernel_stack_top);

    kprintf("[proc] pid %llu: %s, entry 0x%llx, stack 0x%llx, CPL 3\n",
            g_pid, path, entry, user_stack);

    user_enter(entry, user_stack);

    /* user_resume() lands here after exit or a trapped user exception. */
    tss_set_rsp0(BOOT_STACK_TOP);
    g_active = 0;

    vfs_close_all();
    release_user_pages();
    kfree(kernel_stack);

    return g_exit_code;
}

int process_active(void)
{
    return g_active;
}

const struct process_info *process_get_info(void)
{
    static struct process_info info;
    info.active = g_active;
    info.pid = g_pid;
    info.entry = g_entry;
    info.kernel_stack = g_kernel_stack;
    info.user_pages = (u64)g_mapping_count;
    return &info;
}

static int user_range_ok(u64 pointer, u64 length)
{
    if (length == 0)
        return 1;
    if (pointer == 0 || pointer >= USER_ADDRESS_LIMIT)
        return 0;
    u64 last = pointer + length - 1;
    if (last < pointer || last >= USER_ADDRESS_LIMIT)
        return 0;

    u64 page = pointer & ~0xFFFULL;
    u64 last_page = last & ~0xFFFULL;
    for (; page <= last_page; page += PAGE_SIZE) {
        if (!vmm_page_is_user(page))
            return 0;
    }
    return 1;
}

static int copy_user_string(char *destination, usize capacity, u64 source)
{
    if (!capacity)
        return -1;
    for (usize i = 0; i < capacity; i++) {
        if (!user_range_ok(source + i, 1))
            return -1;
        char character = *(const char *)(uintptr_t)(source + i);
        destination[i] = character;
        if (character == 0)
            return 0;
    }
    destination[capacity - 1] = 0;
    return -1;
}

static long syscall_write(u64 descriptor, u64 buffer, u64 length)
{
    if (descriptor != 1 && descriptor != 2)
        return -1;
    if (length > 65536)
        length = 65536;
    if (!user_range_ok(buffer, length))
        return -1;
    console_write_n((const char *)(uintptr_t)buffer, length);
    return (long)length;
}

static long syscall_open(u64 path_pointer)
{
    char path[256];
    if (copy_user_string(path, sizeof(path), path_pointer) != 0)
        return -1;
    return vfs_open(path);
}

static long syscall_read(u64 descriptor, u64 buffer, u64 length)
{
    if (length > 0x100000)
        length = 0x100000;
    if (!user_range_ok(buffer, length))
        return -1;
    return vfs_read((int)descriptor, (void *)(uintptr_t)buffer, length);
}

void syscall_dispatch(struct interrupt_frame *frame)
{
    if (!g_active) {
        frame->rax = (u64)-1;
        return;
    }

    long result = -1;
    switch (frame->rax) {
    case SYS_EXIT:
        g_exit_code = (int)frame->rdi;
        user_resume();
    case SYS_WRITE:
        result = syscall_write(frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_GETPID:
        result = (long)g_pid;
        break;
    case SYS_OPEN:
        result = syscall_open(frame->rdi);
        break;
    case SYS_READ:
        result = syscall_read(frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_CLOSE:
        result = vfs_close((int)frame->rdi);
        break;
    case SYS_UPTIME:
        result = (long)(timer_ticks() * 1000 / timer_hz());
        break;
    default:
        result = -1;
        break;
    }
    frame->rax = (u64)result;
}

void process_handle_exception(struct interrupt_frame *frame)
{
    u64 cr2 = 0;
    if (frame->vector == 14)
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n[proc] user exception %llu at rip 0x%llx, cr2 0x%llx\n",
            frame->vector, frame->rip, cr2);
    console_reset_color();

    g_exit_code = -(int)frame->vector;
    user_resume();
}
