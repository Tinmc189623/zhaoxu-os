/*
 * csh - file mutation commands (write, rm).
 */
#include "csh.h"

static char g_text[1024];

void csh_write(int argc, char **argv)
{
    if (argc < 2) {
        csh_error("usage: write <path> [text...]");
        return;
    }

    u64 used = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && used + 1 < (u64)sizeof(g_text))
            g_text[used++] = ' ';
        for (const char *p = argv[i]; *p && used + 1 < (u64)sizeof(g_text); p++)
            g_text[used++] = *p;
    }
    g_text[used] = 0;

    long result = sys_write_file(argv[1], g_text, used);
    if (result < 0) {
        csh_error("write failed (only /mnt is writable, 8.3 names)");
        return;
    }
    csh_out("wrote ");
    csh_number(result);
    csh_outln(" byte(s)");
}

void csh_rm(int argc, char **argv)
{
    if (argc < 2) {
        csh_error("usage: rm <path>");
        return;
    }
    if (sys_unlink(argv[1]) < 0) {
        csh_error("cannot remove file");
        return;
    }
    csh_outln("removed");
}
