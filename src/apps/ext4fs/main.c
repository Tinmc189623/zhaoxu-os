/*
 * Zhaoxu OS - /bin/ext4fs service entry.
 *
 * The kernel spawns this once, passing the physical address of the shared
 * control page as argv[0].  The service maps 0..4 GiB as user pages, so that
 * physical address is directly usable (same trick as /bin/drivers).
 */
#include <ext4fs.h>
#include <fs_rpc.h>
#include <kprintf.h>
#include <types.h>
#include <userlib.h>

#include "ext4.h"

static u64 parse_hex(const char *text)
{
    u64 value = 0;
    if (!text)
        return 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    while (*text) {
        u64 digit;
        if (*text >= '0' && *text <= '9')
            digit = (u64)(*text - '0');
        else if (*text >= 'a' && *text <= 'f')
            digit = (u64)(*text - 'a' + 10);
        else if (*text >= 'A' && *text <= 'F')
            digit = (u64)(*text - 'A' + 10);
        else
            break;
        value = (value << 4) | digit;
        text++;
    }
    return value;
}

/* Observable mount probe: gives the boot log a definitive answer before the
 * VFS is wired to this service. */
void ext4fs_probe(void)
{
    if (ext4_mount() != 0) {
        console_write("[ext4fs] no ext4 volume\n");
        return;
    }

    char text[96];
    ksnprintf(text, sizeof(text), "[ext4fs] mounted \"%s\"\n", ext4_label());
    console_write(text);
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    if (argc < 1)
        return 1;

    struct fs_rpc *rpc = (struct fs_rpc *)(u64)parse_hex(argv[0]);
    if (!rpc || rpc->magic != FS_RPC_MAGIC)
        return 2;

    u8 *data = (u8 *)(u64)rpc->data_phys;
    ext4fs_service(rpc, data);
    return 0;
}
