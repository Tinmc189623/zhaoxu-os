/*
 * Zhaoxu OS init - the first ring 3 process.
 *
 * It proves the privilege transition, reads files from the initramfs and
 * reads files from the FAT32 data volume through the kernel VFS.
 */
#include "userlib.h"

static char buffer[2048];

static void cat(const char *path)
{
    long fd = sys_open(path, 0);
    if (fd < 0) {
        u_puts("[init] cannot open ");
        u_puts(path);
        u_puts("\n");
        return;
    }

    u_puts("----- ");
    u_puts(path);
    u_puts(" -----\n");
    long count;
    while ((count = sys_read(fd, buffer, sizeof(buffer))) > 0)
        sys_write(1, buffer, (u64)count);
    sys_close(fd);
    u_puts("\n");
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);

    u16 cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));

    u_puts("\n[init] ring 3 alive: CS=");
    u_puthex(cs);
    u_puts(" pid=");
    u_putn(sys_getpid());
    u_puts(" uptime=");
    u_putn(sys_uptime());
    u_puts(" ms\n");

    u_puts("[init] argc=");
    u_putn(argc);
    for (long i = 0; i < argc; i++) {
        u_puts(", argv[");
        u_putn(i);
        u_puts("]=");
        u_puts(argv[i]);
    }
    u_puts("\n");

    cat("/etc/motd");
    cat("/mnt/HELLO.TXT");
    cat("/mnt/DOCS/INFO.TXT");

    u_puts("[init] done, exiting\n");
    return 0;
}
