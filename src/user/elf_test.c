/* Zhaoxu OS - static ELF startup regression (auxv, envp, brk).
 *
 * Built as ET_EXEC at 1 TiB by user-exec.ld, so the kernel takes its ELF
 * path: segments map at fixed addresses, the stack carries envp and an auxv,
 * and brk starts after the last segment.
 */
#include "userlib.h"

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_RANDOM 25
#define AT_EXECFN 31

static int fail(const char *reason)
{
    u_puts("[elf ] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

/* Find one auxv entry by type, or 0 when absent. */
static unsigned long auxv_value(const unsigned long *auxv, unsigned long type,
                                int *seen)
{
    for (int i = 0; auxv[2 * i] != AT_NULL; i++) {
        if (auxv[2 * i] == type) {
            *seen = 1;
            return auxv[2 * i + 1];
        }
    }
    *seen = 0;
    return 0;
}

int user_main(long *stack)
{
    long argc = stack[0];
    char **argv = (char **)&stack[1];
    char **envp = &argv[argc + 1];
    int env_count = 0;
    while (envp[env_count])
        env_count++;
    unsigned long *auxv =
        (unsigned long *)&envp[env_count + 1];

    int seen = 0;
    unsigned long pagesz = auxv_value(auxv, AT_PAGESZ, &seen);
    if (!seen || pagesz != 4096)
        return fail("pagesz");
    /* AT_PHDR is 0 when the linker left the program headers outside every
     * LOAD segment; otherwise it must point into the image. */
    unsigned long phdr = auxv_value(auxv, AT_PHDR, &seen);
    if (!seen || (phdr != 0 && phdr < 0x10000000000UL))
        return fail("phdr");
    unsigned long phent = auxv_value(auxv, AT_PHENT, &seen);
    if (!seen || phent == 0)
        return fail("phent");
    unsigned long entry = auxv_value(auxv, AT_ENTRY, &seen);
    if (!seen || entry < 0x10000000000UL)
        return fail("entry");
    unsigned long random = auxv_value(auxv, AT_RANDOM, &seen);
    if (!seen || random == 0)
        return fail("random");
    unsigned long execfn = auxv_value(auxv, AT_EXECFN, &seen);
    if (!seen || execfn == 0)
        return fail("execfn");
    u_puts("[elf ] auxv PASS\n");

    int path_seen = 0;
    for (int i = 0; i < env_count; i++) {
        const char *text = envp[i];
        if (text[0] == 'P' && text[1] == 'A' && text[2] == 'T' &&
            text[3] == 'H' && text[4] == '=')
            path_seen = 1;
    }
    if (!path_seen)
        return fail("envp");
    u_puts("[elf ] envp PASS\n");

    long base = sys_brk(0);
    if (base <= 0 || (base & 0xfff) != 0)
        return fail("brk-query");
    long grown = sys_brk((void *)(base + 2 * 4096));
    if (grown != base + 2 * 4096)
        return fail("brk-grow");
    volatile unsigned char *heap = (volatile unsigned char *)base;
    heap[0] = 0x77;
    heap[2 * 4096 - 1] = 0x88;
    if (heap[0] != 0x77 || heap[2 * 4096 - 1] != 0x88)
        return fail("brk-write");
    if (sys_brk((void *)base) != base)
        return fail("brk-shrink");
    u_puts("[elf ] brk PASS\n");

    u_puts("[elf ] ELF_TEST exit 0\n");
    return 0;
}
