/*
 * ext4 host harness - command line driver.
 *
 *   ext4-test <image> <command> [arguments] [command ...]
 *
 * Commands: format <sectors>, mount <base>, ls <path>, cat <path>,
 *           write <path> <size>, contig <path> <size>, rm <path>,
 *           stat <path>, sync.
 *
 * Every command runs in the order given, against the one image, so a single
 * invocation can format and mount, and separate invocations prove that the
 * state really reached the image.
 */
#define _GNU_SOURCE
#include "ext4.h"
#include "ext4_env.h" /* ext4_free: ext4_read_all hands back ext4_alloc memory */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int host_env_open(const char *image);

/* ext4_sync() lives in ext4_core.h (the core-internal header).  The harness
 * needs it for the `sync` command but nothing else from the internals, so it
 * is declared here instead of dragging the internal header into the driver. */
int ext4_sync(void);

static void list_cb(const char *name, u64 size, int is_dir, void *ctx)
{
    (void)ctx;
    printf("%c %llu %s\n", is_dir ? 'd' : 'f',
           (unsigned long long)size, name);
}

int main(int argc, char **argv)
{
    if (argc < 2 || host_env_open(argv[1]) != 0)
        return 2;

    for (int i = 2; i < argc; i++) {
        char *command = argv[i];

        if (!strcmp(command, "format") && i + 1 < argc) {
            u32 sectors = (u32)strtoul(argv[++i], 0, 0);
            if (ext4_format_volume(0, 0, sectors, "ZTEST") != 0)
                return 12;
            if (ext4_mount_at(0, 0) != 0)
                return 13;
        } else if (!strcmp(command, "mount") && i + 1 < argc) {
            if (ext4_mount_at(0, strtoull(argv[++i], 0, 0)) != 0)
                return 3;
        } else if (!strcmp(command, "ls") && i + 1 < argc) {
            if (ext4_list(argv[++i], list_cb, 0) != 0)
                return 4;
        } else if (!strcmp(command, "cat") && i + 1 < argc) {
            u8 *data = 0;
            u64 size = 0;
            if (ext4_read_all(argv[++i], &data, &size) != 0)
                return 5;
            if (size && fwrite(data, 1, (size_t)size, stdout) != size)
                return 5;
            ext4_free(data);
        } else if (!strcmp(command, "write") && i + 2 < argc) {
            const char *path = argv[++i];
            u64 size = strtoull(argv[++i], 0, 0);
            u8 *data = malloc((size_t)(size ? size : 1));
            if (!data)
                return 6;
            for (u64 k = 0; k < size; k++)
                data[k] = (u8)('a' + k % 26);
            int result = ext4_write_file(path, data, size);
            free(data);
            if (result != 0)
                return 6;
        } else if (!strcmp(command, "contig") && i + 2 < argc) {
            const char *path = argv[++i];
            u64 size = strtoull(argv[++i], 0, 0);
            u8 *data = malloc((size_t)(size ? size : 1));
            if (!data)
                return 7;
            for (u64 k = 0; k < size; k++)
                data[k] = (u8)(k & 0xFF);
            u32 first = 0;
            u32 blocks = 0;
            int result = ext4_write_file_contig(path, data, size, &first,
                                                &blocks);
            free(data);
            if (result != 0)
                return 7;
            printf("contig %u %u\n", first, blocks);
        } else if (!strcmp(command, "rm") && i + 1 < argc) {
            if (ext4_unlink(argv[++i]) != 0)
                return 8;
        } else if (!strcmp(command, "stat") && i + 1 < argc) {
            int is_dir = 0;
            u64 size = 0;
            if (ext4_stat(argv[++i], &is_dir, &size) != 0)
                return 9;
            printf("%c %llu\n", is_dir ? 'd' : 'f',
                   (unsigned long long)size);
        } else if (!strcmp(command, "sync")) {
            if (ext4_sync() != 0)
                return 10;
        } else {
            fprintf(stderr, "bad command %s\n", command);
            return 11;
        }
    }
    return 0;
}
