#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <string.h>
#include <types.h>
#include <vfs.h>
#include <zxe_format.h>
#include "process_internal.h"

#define ZXE_MODULE_LIMIT (ZXE_MAX_DEPENDENCIES + 1)
#define ZXE_USER_END (0x0000010000000000ULL + ZXE_IMAGE_WINDOW)
#define ZXE_USER_BASE 0x0000010000000000ULL
#define ZXE_STACK_LOW 0x00000100FFC00000ULL

struct zxe_module {
    const u8 *image;
    u64 file_size;
    int owns_image;
    int visit_state;
    u64 base;
    char path[256];
    const struct zxe_header *header;
    const u8 *tables[7];
    u32 counts[7];
    int dependency_modules[ZXE_MAX_DEPENDENCIES];
};

struct zxe_context {
    struct process *process;
    struct zxe_module modules[ZXE_MODULE_LIMIT];
    u32 module_count;
    u64 rng;
    int has_rdrand;
};

static u32 table_entry_size(u32 kind)
{
    switch (kind) {
    case ZXE_TABLE_SEGMENTS: return sizeof(struct zxe_segment);
    case ZXE_TABLE_DEPENDENCIES: return sizeof(struct zxe_dependency);
    case ZXE_TABLE_IMPORTS: return sizeof(struct zxe_import);
    case ZXE_TABLE_EXPORTS: return sizeof(struct zxe_export);
    case ZXE_TABLE_RELOCATIONS: return sizeof(struct zxe_relocation);
    case ZXE_TABLE_STRINGS: return 1;
    default: return 0;
    }
}

static u32 table_limit(u32 kind)
{
    switch (kind) {
    case ZXE_TABLE_SEGMENTS: return ZXE_MAX_SEGMENTS;
    case ZXE_TABLE_DEPENDENCIES: return ZXE_MAX_DEPENDENCIES;
    case ZXE_TABLE_IMPORTS: return ZXE_MAX_IMPORTS;
    case ZXE_TABLE_EXPORTS: return ZXE_MAX_EXPORTS;
    case ZXE_TABLE_RELOCATIONS: return ZXE_MAX_RELOCATIONS;
    case ZXE_TABLE_STRINGS: return ZXE_MAX_STRINGS;
    default: return 0;
    }
}

static int valid_utf8(const u8 *text, u32 length)
{
    u32 i = 0;
    while (i < length) {
        u8 a = text[i++];
        if (a < 0x80)
            continue;
        if (a >= 0xC2 && a <= 0xDF) {
            if (i >= length || (text[i++] & 0xC0) != 0x80)
                return 0;
        } else if (a >= 0xE0 && a <= 0xEF) {
            if (i + 1 >= length)
                return 0;
            u8 b = text[i++], c = text[i++];
            if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 ||
                (a == 0xE0 && b < 0xA0) || (a == 0xED && b >= 0xA0))
                return 0;
        } else if (a >= 0xF0 && a <= 0xF4) {
            if (i + 2 >= length)
                return 0;
            u8 b = text[i++], c = text[i++], d = text[i++];
            if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 ||
                (d & 0xC0) != 0x80 || (a == 0xF0 && b < 0x90) ||
                (a == 0xF4 && b >= 0x90))
                return 0;
        } else {
            return 0;
        }
    }
    return 1;
}

static const char *module_string(const struct zxe_module *module, u32 offset)
{
    const u8 *strings = module->tables[ZXE_TABLE_STRINGS];
    u32 length = module->counts[ZXE_TABLE_STRINGS];
    if (!strings || offset >= length)
        return NULL;
    for (u32 i = offset; i < length && i - offset <= 255; i++) {
        if (!strings[i])
            return i == offset || !valid_utf8(strings + offset, i - offset)
                       ? NULL : (const char *)(strings + offset);
    }
    return NULL;
}

static int valid_library_name(const char *name)
{
    if (!name || !name[0] || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return 0;
    for (u32 i = 0; name[i]; i++) {
        char c = name[i];
        if (i >= 250 || c == '/' || c == '\\' || c == ':' || c < 0x21)
            return 0;
    }
    return 1;
}

static int parse_module(struct zxe_module *module, int shared)
{
    if (!module->image || module->file_size < sizeof(struct zxe_header) ||
        module->file_size > ZXE_MAX_FILE_SIZE)
        return -1;
    const struct zxe_header *h = (const struct zxe_header *)module->image;
    if (memcmp(h->magic, ZXE_MAGIC, 4) != 0 ||
        h->version != ZXE_VERSION || h->machine != ZXE_MACHINE_X86_64 ||
        h->header_size != ZXE_HEADER_SIZE || h->api_abi_major != 1 ||
        h->reserved0 || h->file_size != module->file_size ||
        (h->flags & ~ZXE_FLAG_SHARED) ||
        ((h->flags & ZXE_FLAG_SHARED) != 0) != shared ||
        h->directory_entry_size != ZXE_DIRECTORY_ENTRY_SIZE ||
        !h->directory_count || h->directory_count > ZXE_MAX_TABLES ||
        h->directory_offset != ZXE_HEADER_SIZE || (h->directory_offset & 7) ||
        !h->image_size || h->image_size > ZXE_IMAGE_WINDOW ||
        (shared ? h->entry_rva != 0 : h->entry_rva >= h->image_size))
        return -1;
    for (u32 i = 0; i < sizeof(h->reserved1); i++)
        if (h->reserved1[i])
            return -1;

    u64 dir_bytes = (u64)h->directory_count * sizeof(struct zxe_directory_entry);
    u64 dir_end = h->directory_offset + dir_bytes;
    if (dir_end < h->directory_offset || dir_end > module->file_size)
        return -1;
    const struct zxe_directory_entry *dirs =
        (const struct zxe_directory_entry *)(module->image + h->directory_offset);
    u64 starts[ZXE_MAX_TABLES], ends[ZXE_MAX_TABLES];
    u64 metadata_end = dir_end;
    for (u32 i = 0; i < h->directory_count; i++) {
        const struct zxe_directory_entry *d = &dirs[i];
        if ((d->flags & ~ZXE_DIR_REQUIRED) || (d->offset & 7) ||
            d->offset < dir_end)
            return -1;
        for (u32 j = 0; j < i; j++)
            if (dirs[j].kind == d->kind)
                return -1;
        u32 expected = table_entry_size(d->kind);
        if (!expected) {
            if ((d->flags & ZXE_DIR_REQUIRED) || !d->entry_size)
                return -1;
        } else if (d->entry_size != expected ||
                   d->count > table_limit(d->kind)) {
            return -1;
        }
        u64 bytes = (u64)d->count * d->entry_size;
        u64 end = d->offset + bytes;
        if (end < d->offset || end > module->file_size)
            return -1;
        starts[i] = d->offset;
        ends[i] = end;
        if (end > metadata_end)
            metadata_end = end;
        if (d->kind <= ZXE_TABLE_STRINGS) {
            module->tables[d->kind] = module->image + d->offset;
            module->counts[d->kind] = d->count;
        }
    }
    for (u32 i = 0; i < h->directory_count; i++) {
        for (u32 j = 0; j < i; j++)
            if (starts[i] < ends[j] && starts[j] < ends[i])
                return -1;
    }
    if (!module->tables[ZXE_TABLE_SEGMENTS] ||
        !module->counts[ZXE_TABLE_SEGMENTS] ||
        !module->tables[ZXE_TABLE_STRINGS])
        return -1;

    const struct zxe_segment *segments =
        (const struct zxe_segment *)module->tables[ZXE_TABLE_SEGMENTS];
    u64 max_rva = 0;
    int entry_exec = shared;
    for (u32 i = 0; i < module->counts[ZXE_TABLE_SEGMENTS]; i++) {
        const struct zxe_segment *s = &segments[i];
        u32 perms;
        switch (s->type) {
        case ZXE_SEG_CODE: perms = ZXE_PERM_R | ZXE_PERM_X; break;
        case ZXE_SEG_RODATA: perms = ZXE_PERM_R; break;
        case ZXE_SEG_DATA:
        case ZXE_SEG_ZERO: perms = ZXE_PERM_R | ZXE_PERM_W; break;
        default: return -1;
        }
        if (s->flags != perms || (s->flags & (ZXE_PERM_W | ZXE_PERM_X)) ==
                                   (ZXE_PERM_W | ZXE_PERM_X) ||
            s->alignment != PAGE_SIZE || (s->vaddr_rva & (PAGE_SIZE - 1)) ||
            !s->mem_size || (s->mem_size & (PAGE_SIZE - 1)) ||
            s->file_size > s->mem_size ||
            (s->type == ZXE_SEG_ZERO && s->file_size) ||
            (s->type != ZXE_SEG_ZERO && !s->file_size) ||
            s->vaddr_rva + s->mem_size < s->vaddr_rva ||
            s->vaddr_rva + s->mem_size > h->image_size)
            return -1;
        if ((s->file_offset & 7) || s->file_offset < metadata_end ||
            s->file_offset > module->file_size ||
            (s->file_size &&
             (s->file_offset + s->file_size < s->file_offset ||
              s->file_offset + s->file_size > module->file_size)))
            return -1;
        u64 end = s->vaddr_rva + s->mem_size;
        if (end > max_rva)
            max_rva = end;
        if (!shared && s->type == ZXE_SEG_CODE && h->entry_rva >= s->vaddr_rva &&
            h->entry_rva < end)
            entry_exec = 1;
        for (u32 j = 0; j < i; j++) {
            const struct zxe_segment *p = &segments[j];
            if (s->vaddr_rva < p->vaddr_rva + p->mem_size &&
                p->vaddr_rva < end)
                return -1;
            if (s->file_size && p->file_size &&
                s->file_offset < p->file_offset + p->file_size &&
                p->file_offset < s->file_offset + s->file_size)
                return -1;
        }
    }
    if (max_rva != h->image_size || !entry_exec)
        return -1;
    module->header = h;

    const struct zxe_dependency *deps =
        (const struct zxe_dependency *)module->tables[ZXE_TABLE_DEPENDENCIES];
    for (u32 i = 0; i < module->counts[ZXE_TABLE_DEPENDENCIES]; i++) {
        const char *name = module_string(module, deps[i].name_offset);
        if (!valid_library_name(name) || !deps[i].abi_major || deps[i].flags ||
            deps[i].reserved)
            return -1;
    }
    const struct zxe_import *imports =
        (const struct zxe_import *)module->tables[ZXE_TABLE_IMPORTS];
    for (u32 i = 0; i < module->counts[ZXE_TABLE_IMPORTS]; i++) {
        const struct zxe_import *s = &imports[i];
        if (s->dependency_index >= module->counts[ZXE_TABLE_DEPENDENCIES] ||
            !module_string(module, s->name_offset) || !s->abi_major ||
            (s->flags & ~ZXE_IMPORT_WEAK) ||
            s->abi_major != deps[s->dependency_index].abi_major)
            return -1;
    }
    const struct zxe_export *exports =
        (const struct zxe_export *)module->tables[ZXE_TABLE_EXPORTS];
    for (u32 i = 0; i < module->counts[ZXE_TABLE_EXPORTS]; i++) {
        const struct zxe_export *s = &exports[i];
        if (!module_string(module, s->name_offset) || !s->abi_major ||
            s->flags != ZXE_EXPORT_GLOBAL || s->value_rva >= h->image_size ||
            (s->size && s->size > h->image_size - s->value_rva))
            return -1;
        int in_segment = 0;
        for (u32 j = 0; j < module->counts[ZXE_TABLE_SEGMENTS]; j++) {
            const struct zxe_segment *segment = &segments[j];
            if (s->value_rva >= segment->vaddr_rva &&
                s->value_rva < segment->vaddr_rva + segment->mem_size &&
                (!s->size || s->size <= segment->vaddr_rva + segment->mem_size -
                                       s->value_rva))
                in_segment = 1;
        }
        if (!in_segment)
            return -1;
        for (u32 j = 0; j < i; j++) {
            const struct zxe_export *p = &exports[j];
            if (s->abi_major == p->abi_major &&
                strcmp(module_string(module, s->name_offset),
                       module_string(module, p->name_offset)) == 0)
                return -1;
        }
    }
    const struct zxe_relocation *relocs =
        (const struct zxe_relocation *)module->tables[ZXE_TABLE_RELOCATIONS];
    for (u32 i = 0; i < module->counts[ZXE_TABLE_RELOCATIONS]; i++) {
        const struct zxe_relocation *r = &relocs[i];
        if (r->type < ZXE_RELOC_RELATIVE || r->type > ZXE_RELOC_JUMP_SLOT ||
            (r->target_rva & 7) || r->target_rva + 8 < r->target_rva ||
            r->target_rva + 8 > h->image_size ||
            (r->type == ZXE_RELOC_RELATIVE && r->symbol_index) ||
            (r->type != ZXE_RELOC_RELATIVE &&
             r->symbol_index >= module->counts[ZXE_TABLE_IMPORTS]))
            return -1;
        int writable = 0;
        for (u32 j = 0; j < module->counts[ZXE_TABLE_SEGMENTS]; j++) {
            const struct zxe_segment *s = &segments[j];
            if ((s->type == ZXE_SEG_DATA || s->type == ZXE_SEG_ZERO) &&
                r->target_rva >= s->vaddr_rva &&
                r->target_rva + 8 <= s->vaddr_rva + s->mem_size)
                writable = 1;
        }
        if (!writable)
            return -1;
    }
    return 0;
}

static int find_module(struct zxe_context *ctx, const char *path)
{
    for (u32 i = 0; i < ctx->module_count; i++)
        if (strcmp(ctx->modules[i].path, path) == 0)
            return (int)i;
    return -1;
}

static int load_dependencies(struct zxe_context *ctx, u32 root)
{
    u32 stack[ZXE_MODULE_LIMIT];
    u32 next_dependency[ZXE_MODULE_LIMIT];
    u32 depth = 1;
    stack[0] = root;
    next_dependency[0] = 0;
    ctx->modules[root].visit_state = 1;

    while (depth) {
        u32 index = stack[depth - 1];
        struct zxe_module *module = &ctx->modules[index];
        u32 dep_count = module->counts[ZXE_TABLE_DEPENDENCIES];
        if (next_dependency[depth - 1] == dep_count) {
            module->visit_state = 2;
            depth--;
            continue;
        }

        u32 dep_index = next_dependency[depth - 1]++;
        const struct zxe_dependency *deps =
            (const struct zxe_dependency *)module->tables[ZXE_TABLE_DEPENDENCIES];
        const struct zxe_dependency *dep = &deps[dep_index];
        const char *name = module_string(module, dep->name_offset);
        char path[256];
        const char prefix[] = "/lib/";
        u32 p = 0;
        while (prefix[p]) { path[p] = prefix[p]; p++; }
        u32 n = 0;
        while (name[n] && p + n < sizeof(path) - 1) {
            path[p + n] = name[n];
            n++;
        }
        if (name[n])
            return -1;
        path[p + n] = 0;

        int child = find_module(ctx, path);
        if (child < 0) {
            if (ctx->module_count >= ZXE_MODULE_LIMIT)
                return -1;
            u8 *bytes = NULL;
            u64 size = 0;
            if (vfs_read_all(path, &bytes, &size) != 0)
                return -1;
            child = (int)ctx->module_count++;
            struct zxe_module *loaded = &ctx->modules[child];
            memset(loaded, 0, sizeof(*loaded));
            loaded->image = bytes;
            loaded->file_size = size;
            loaded->owns_image = 1;
            for (u32 c = 0; path[c] && c + 1 < sizeof(loaded->path); c++)
                loaded->path[c] = path[c];
            if (parse_module(loaded, 1) != 0) {
                kfree(bytes);
                loaded->image = NULL;
                loaded->owns_image = 0;
                return -1;
            }
        }
        module = &ctx->modules[index];
        module->dependency_modules[dep_index] = child;
        struct zxe_module *library = &ctx->modules[child];
        const struct zxe_export *exports =
            (const struct zxe_export *)library->tables[ZXE_TABLE_EXPORTS];
        int has_abi = 0;
        for (u32 e = 0; e < library->counts[ZXE_TABLE_EXPORTS]; e++)
            if (exports[e].abi_major == dep->abi_major)
                has_abi = 1;
        if (!has_abi || library->visit_state == 1)
            return -1;
        if (library->visit_state == 0) {
            if (depth >= ZXE_MODULE_LIMIT)
                return -1;
            library->visit_state = 1;
            stack[depth] = (u32)child;
            next_dependency[depth] = 0;
            depth++;
        }
    }
    return 0;
}

static u64 random_next(struct zxe_context *ctx)
{
    if (ctx->has_rdrand) {
        for (u32 i = 0; i < 10; i++) {
            u64 hardware;
            u8 ready;
            __asm__ volatile("rdrand %0; setc %1"
                             : "=r"(hardware), "=qm"(ready));
            if (ready) {
                ctx->rng ^= hardware;
                return hardware;
            }
        }
    }
    u64 x = ctx->rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    ctx->rng = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int assign_bases(struct zxe_context *ctx)
{
    u64 total_span = 0;
    for (u32 i = 0; i < ctx->module_count; i++) {
        u64 span = ALIGN_UP(ctx->modules[i].header->image_size, 0x10000ULL);
        if (span > ZXE_IMAGE_WINDOW - total_span)
            return -1;
        total_span += span;
    }
    u32 low, high, eax, ebx, ecx, edx;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    ctx->has_rdrand = (ecx & (1U << 30)) != 0;
    (void)eax;
    (void)ebx;
    (void)edx;
    ctx->rng = ((u64)high << 32) | low;
    ctx->rng ^= ctx->process->pid * 0x9E3779B97F4A7C15ULL;
    if (!ctx->rng)
        ctx->rng = 0xD1B54A32D192ED03ULL;
    for (u32 i = 0; i < ctx->module_count; i++) {
        struct zxe_module *module = &ctx->modules[i];
        u64 span = ALIGN_UP(module->header->image_size, 0x10000ULL);
        u64 slots = (ZXE_IMAGE_WINDOW - span) / 0x10000ULL + 1;
        int placed = 0;
        for (u32 attempt = 0; attempt < 64; attempt++) {
            u64 base = ZXE_USER_BASE + (random_next(ctx) % slots) * 0x10000ULL;
            if (base + span > ZXE_USER_END ||
                (base < 0x0000010100000000ULL &&
                 base + span > ZXE_STACK_LOW))
                continue;
            int overlap = 0;
            for (u32 j = 0; j < i; j++) {
                u64 other = ctx->modules[j].base;
                u64 other_end = other +
                    ALIGN_UP(ctx->modules[j].header->image_size, 0x10000ULL);
                if (base < other_end && other < base + span)
                    overlap = 1;
            }
            if (!overlap) {
                module->base = base;
                placed = 1;
                break;
            }
        }
        if (!placed)
            return -1;
    }
    return 0;
}

static int map_modules(struct zxe_context *ctx)
{
    for (u32 i = 0; i < ctx->module_count; i++) {
        struct zxe_module *module = &ctx->modules[i];
        const struct zxe_segment *segments =
            (const struct zxe_segment *)module->tables[ZXE_TABLE_SEGMENTS];
        for (u32 j = 0; j < module->counts[ZXE_TABLE_SEGMENTS]; j++) {
            const struct zxe_segment *s = &segments[j];
            u64 address = module->base + s->vaddr_rva;
            u64 flags = 0;
            if (s->flags & ZXE_PERM_W)
                flags |= VMM_WRITABLE;
            if (!(s->flags & ZXE_PERM_X))
                flags |= VMM_EXECUTE_DISABLE;
            for (u64 offset = 0; offset < s->mem_size; offset += PAGE_SIZE)
                if (!process_map_page(ctx->process, address + offset, flags))
                    return -1;
            if (s->file_size && process_user_write(ctx->process, address,
                    module->image + s->file_offset, s->file_size) != 0)
                return -1;
        }
    }
    return 0;
}

static int resolve_import(struct zxe_context *ctx, struct zxe_module *module,
                          u32 index, u64 *value, int *weak_missing)
{
    const struct zxe_import *imports =
        (const struct zxe_import *)module->tables[ZXE_TABLE_IMPORTS];
    const struct zxe_import *imp = &imports[index];
    struct zxe_module *library =
        &ctx->modules[module->dependency_modules[imp->dependency_index]];
    const struct zxe_export *exports =
        (const struct zxe_export *)library->tables[ZXE_TABLE_EXPORTS];
    const char *wanted = module_string(module, imp->name_offset);
    int found = 0;
    *value = 0;
    for (u32 i = 0; i < library->counts[ZXE_TABLE_EXPORTS]; i++) {
        const struct zxe_export *e = &exports[i];
        if (e->abi_major == imp->abi_major &&
            strcmp(module_string(library, e->name_offset), wanted) == 0) {
            if (found)
                return -1;
            *value = library->base + e->value_rva;
            found = 1;
        }
    }
    *weak_missing = !found && (imp->flags & ZXE_IMPORT_WEAK);
    return found || *weak_missing ? 0 : -1;
}

static u64 add_signed(u64 base, i64 addend, int *ok)
{
    if (addend >= 0) {
        u64 n = (u64)addend;
        if (base > ~(u64)0 - n) { *ok = 0; return 0; }
        return base + n;
    }
    u64 n = (u64)(-(addend + 1)) + 1;
    if (base < n) { *ok = 0; return 0; }
    return base - n;
}

static int apply_relocations(struct zxe_context *ctx)
{
    for (u32 i = 0; i < ctx->module_count; i++) {
        struct zxe_module *module = &ctx->modules[i];
        const struct zxe_relocation *relocs =
            (const struct zxe_relocation *)module->tables[ZXE_TABLE_RELOCATIONS];
        for (u32 j = 0; j < module->counts[ZXE_TABLE_RELOCATIONS]; j++) {
            const struct zxe_relocation *r = &relocs[j];
            int ok = 1, weak = 0;
            u64 symbol = 0, result;
            if (r->type == ZXE_RELOC_RELATIVE) {
                result = add_signed(module->base, r->addend, &ok);
            } else {
                if (resolve_import(ctx, module, r->symbol_index,
                                   &symbol, &weak) != 0)
                    return -1;
                if (weak && r->type == ZXE_RELOC_64)
                    result = add_signed(0, r->addend, &ok);
                else if (r->type == ZXE_RELOC_64)
                    result = add_signed(symbol, r->addend, &ok);
                else
                    result = symbol;
            }
            if (!ok || (!weak &&
                (result < ZXE_USER_BASE || result >= ZXE_USER_END)))
                return -1;
            if (process_user_write(ctx->process,
                    module->base + r->target_rva, &result, sizeof(result)) != 0)
                return -1;
        }
    }
    return 0;
}

static void release_context(struct zxe_context *ctx)
{
    if (!ctx)
        return;
    for (u32 i = 0; i < ctx->module_count; i++)
        if (ctx->modules[i].owns_image && ctx->modules[i].image)
            kfree((void *)ctx->modules[i].image);
    kfree(ctx);
}

int zxe_load_image(struct process *process, const u8 *image, u64 image_size,
                   const char *path, u64 *entry_out)
{
    struct zxe_context *ctx = kzalloc(sizeof(*ctx));
    if (!ctx)
        return -1;
    ctx->process = process;
    ctx->module_count = 1;
    struct zxe_module *main = &ctx->modules[0];
    main->image = image;
    main->file_size = image_size;
    if (path) {
        u32 i = 0;
        while (path[i] && i + 1 < sizeof(main->path)) {
            main->path[i] = path[i];
            i++;
        }
        if (path[i]) {
            release_context(ctx);
            return -1;
        }
    }
    if (parse_module(main, 0) != 0 ||
        load_dependencies(ctx, 0) != 0 ||
        assign_bases(ctx) != 0 || map_modules(ctx) != 0 ||
        apply_relocations(ctx) != 0) {
        release_context(ctx);
        return -1;
    }
    *entry_out = ctx->modules[0].base + ctx->modules[0].header->entry_rva;
    release_context(ctx);
    return 0;
}
