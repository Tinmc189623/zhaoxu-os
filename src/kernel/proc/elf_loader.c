/*
 * Zhaoxu OS - ZXE/ELF image loading and the initial user stack.
 *
 * The stack follows the x86_64 SysV layout: argc, argv, envp, then an auxv
 * with PHDR/ENTRY/PAGESZ/RANDOM/EXECFN so a C library can start without
 * guessing.  ZXE images read only argc/argv and ignore the rest.
 */
#include "process_internal.h"
#include "elf_loader.h"

#include <mem.h>
#include <random.h>
#include <string.h>
#include <types.h>
#include <zxe_format.h>

#define AUX_AT_NULL   0
#define AUX_AT_PHDR   3
#define AUX_AT_PHENT  4
#define AUX_AT_PHNUM  5
#define AUX_AT_PAGESZ 6
#define AUX_AT_BASE   7
#define AUX_AT_ENTRY  9
#define AUX_AT_UID    11
#define AUX_AT_EUID   12
#define AUX_AT_GID    13
#define AUX_AT_EGID   14
#define AUX_AT_RANDOM 25
#define AUX_AT_EXECFN 31

#define STACK_ARGV_MAX 16
#define STACK_ENVP_MAX 8
#define STACK_POINTERS_MAX 64
#define ELF_BRK_HEADROOM (256ULL * 1024 * 1024)

static const char *const default_environment[] = {
    "PATH=/bin:/usr/bin",
    "HOME=/root",
    "USER=root",
    "SHELL=/bin/csh",
    "TERM=zhaoxu",
};

/* What the loader tells the stack builder about the image just mapped. */
struct image_context {
    u64 phdr;
    u64 phent;
    u64 phnum;
};

/* Zero a user range through process_user_write. */
static int user_zero(struct process *process, u64 virtual_address, u64 length)
{
    static const u8 zeros[PAGE_SIZE];
    while (length) {
        u64 chunk = length < PAGE_SIZE ? length : PAGE_SIZE;
        if (process_user_write(process, virtual_address, zeros, chunk) != 0)
            return -1;
        virtual_address += chunk;
        length -= chunk;
    }
    return 0;
}

static int elf_load(struct process *process, const u8 *image, u64 image_size,
                    u64 *entry_out, struct image_context *context)
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

    u64 image_end = 0;
    u64 phdr_address = 0;
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

        u64 page_flags = (program->flags & 2) ? VMM_WRITABLE : 0;
        if (!(program->flags & 1))
            page_flags |= VMM_EXECUTE_DISABLE;
        if ((program->flags & 3) == 3)
            return -1;
        for (u64 page = start; page < end; page += PAGE_SIZE) {
            if (!process_map_page(process, page, page_flags))
                return -1;
        }

        if (program->file_size &&
            process_user_write(process, program->virtual_address,
                       image + program->offset, program->file_size) != 0)
            return -1;
        if (program->memory_size > program->file_size &&
            user_zero(process,
                      program->virtual_address + program->file_size,
                      program->memory_size - program->file_size) != 0)
            return -1;

        if (program->virtual_address + program->memory_size > image_end)
            image_end = program->virtual_address + program->memory_size;

        /* AT_PHDR points at the headers only when a segment maps them; lld
         * leaves them outside every LOAD in this layout, so 0 is reported. */
        u64 phoff = header->program_header_offset;
        if (!phdr_address && phoff >= program->offset &&
            phoff < program->offset + program->file_size)
            phdr_address = program->virtual_address + (phoff - program->offset);
    }

    if (header->entry < USER_LOAD_BASE ||
        header->entry >= USER_ADDRESS_LIMIT)
        return -1;

    /* The break starts on the page after the last segment and may grow into
     * the image window only, never towards the stack or module windows. */
    u64 brk = ALIGN_UP(image_end, PAGE_SIZE);
    process->brk_start = brk;
    process->brk_current = brk;
    u64 limit = ALIGN_DOWN(brk + ELF_BRK_HEADROOM, PAGE_SIZE);
    if (limit > USER_LOAD_BASE + 0x40000000ULL)
        limit = USER_LOAD_BASE + 0x40000000ULL;
    process->brk_limit = limit;

    context->phdr = phdr_address;
    context->phent = header->program_header_size;
    context->phnum = header->program_header_count;
    *entry_out = header->entry;
    return 0;
}

/* Push one NUL-terminated string above `stack` and return its address. */
static int push_string(struct process *process, u64 *stack, const char *text,
                       u64 *address)
{
    usize length = strlen(text) + 1;
    if (*stack < USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE + length)
        return -1;
    *stack -= length;
    if (process_user_write(process, *stack, text, length) != 0)
        return -1;
    *address = *stack;
    return 0;
}

static u64 build_user_stack(struct process *process, int argc,
                            const char **argv, const struct image_context *ctx,
                            u64 entry)
{
    u64 stack = USER_STACK_TOP - 16;
    u64 argv_pointers[STACK_ARGV_MAX];
    u64 envp_pointers[STACK_ENVP_MAX];
    u64 auxv[2 * 16];
    u32 aux_count = 0;

    if (argc > STACK_ARGV_MAX)
        argc = STACK_ARGV_MAX;
    if (argc <= 0)
        return 0;

    for (int i = argc - 1; i >= 0; i--) {
        if (push_string(process, &stack, argv[i], &argv_pointers[i]) != 0)
            return 0;
    }

    u32 env_count = sizeof(default_environment) / sizeof(default_environment[0]);
    if (env_count > STACK_ENVP_MAX)
        env_count = STACK_ENVP_MAX;
    for (int i = (int)env_count - 1; i >= 0; i--) {
        if (push_string(process, &stack, default_environment[i],
                        &envp_pointers[i]) != 0)
            return 0;
    }

    stack -= 16;
    u8 seed[16];
    random_bytes(seed, sizeof(seed));
    if (process_user_write(process, stack, seed, sizeof(seed)) != 0)
        return 0;
    u64 random_address = stack;

    stack &= ~0xFULL;

#define PUSH_AUX(type, value)                       \
    do {                                            \
        auxv[2 * aux_count] = (type);               \
        auxv[2 * aux_count + 1] = (u64)(value);     \
        aux_count++;                                \
    } while (0)

    PUSH_AUX(AUX_AT_PHDR, ctx->phdr);
    PUSH_AUX(AUX_AT_PHENT, ctx->phent);
    PUSH_AUX(AUX_AT_PHNUM, ctx->phnum);
    PUSH_AUX(AUX_AT_PAGESZ, PAGE_SIZE);
    PUSH_AUX(AUX_AT_BASE, 0);
    PUSH_AUX(AUX_AT_ENTRY, entry);
    PUSH_AUX(AUX_AT_UID, 0);
    PUSH_AUX(AUX_AT_EUID, 0);
    PUSH_AUX(AUX_AT_GID, 0);
    PUSH_AUX(AUX_AT_EGID, 0);
    PUSH_AUX(AUX_AT_RANDOM, random_address);
    PUSH_AUX(AUX_AT_EXECFN, argv_pointers[0]);
    PUSH_AUX(AUX_AT_NULL, 0);
#undef PUSH_AUX

    u64 pointers[STACK_POINTERS_MAX];
    u32 count = 0;
    pointers[count++] = (u64)argc;
    for (int i = 0; i < argc; i++)
        pointers[count++] = argv_pointers[i];
    pointers[count++] = 0;
    for (u32 i = 0; i < env_count; i++)
        pointers[count++] = envp_pointers[i];
    pointers[count++] = 0;
    for (u32 i = 0; i < 2 * aux_count; i++)
        pointers[count++] = auxv[i];

    stack -= (u64)count * sizeof(u64);
    if (stack < USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE)
        return 0;
    if (process_user_write(process, stack, pointers,
                           (u64)count * sizeof(u64)) != 0)
        return 0;
    return stack;
}

int image_load(struct process *process, const u8 *image, u64 image_size,
               const char *path, int argc, const char **argv,
               u64 *entry_out, u64 *stack_out)
{
    u64 entry = 0;
    struct image_context context;
    memset(&context, 0, sizeof(context));

    if (image_size >= 4 && memcmp(image, ZXE_MAGIC, 4) == 0) {
        if (zxe_load_image(process, image, image_size, path, &entry) != 0)
            return -1;
    } else if (image_size >= 3 && image[0] == 'Z' && image[1] == 'X' &&
               image[2] == 'E') {
        return -1;
    } else if (elf_load(process, image, image_size, &entry, &context) != 0) {
        return -1;
    }

    for (u64 page = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
         page < USER_STACK_TOP; page += PAGE_SIZE) {
        if (!process_map_page(process, page,
                              VMM_WRITABLE | VMM_EXECUTE_DISABLE))
            return -1;
    }

    u64 stack = build_user_stack(process, argc, argv, &context, entry);
    if (!stack)
        return -1;
    *entry_out = entry;
    *stack_out = stack;
    return 0;
}
