/*
 * csh - file commands: list a directory and print a file.
 */
#include "csh.h"

static char g_listing[4096];
static char g_chunk[512];

static void print_entry(char *line)
{
    if (line[0] == 'd' && line[1] == ' ') {
        csh_out("[dir]  ");
        csh_outln(line + 2);
        return;
    }
    if (line[0] != 'f' || line[1] != ' ')
        return;

    char *name = line + 2;
    char *cursor = name;
    while (*cursor && *cursor != ' ')
        cursor++;
    if (*cursor)
        *cursor++ = 0;

    csh_out("[file] ");
    csh_out(name);
    if (*cursor) {
        csh_out("  ");
        csh_out(cursor);
        csh_out(" bytes");
    }
    csh_out("\n");
}

void csh_ls(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/";
    long count = sys_readdir(path, g_listing, sizeof(g_listing) - 1);
    if (count < 0) {
        csh_error("cannot list directory");
        return;
    }
    g_listing[count] = 0;

    char *line = g_listing;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n')
            end++;
        int more = (*end == '\n');
        if (*end)
            *end = 0;
        print_entry(line);
        if (!more)
            break;
        line = end + 1;
    }
}

void csh_cat(int argc, char **argv)
{
    if (argc < 2) {
        csh_error("usage: cat <path>");
        return;
    }

    long fd = sys_open(argv[1], 0);
    if (fd < 0) {
        csh_error("cannot open file");
        return;
    }

    long count;
    while ((count = sys_read(fd, g_chunk, sizeof(g_chunk))) > 0)
        sys_write(1, g_chunk, (u64)count);
    sys_close(fd);
}
