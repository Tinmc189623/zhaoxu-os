/* Zhaoxu OS - demo executable stored on the FAT32 volume. */
#include "userlib.h"

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);

    u16 cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));

    u_puts("[hello] executed from FAT32, CS=");
    u_puthex(cs);
    u_puts(", argc=");
    u_putn(argc);
    if (argc > 0) {
        u_puts(", argv[0]=");
        u_puts(argv[0]);
    }
    u_puts("\n[hello] returning status 7\n");
    return 7;
}
