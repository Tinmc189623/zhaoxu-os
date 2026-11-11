/*
 * csh - disk listing and the installer front end.
 *
 * The kernel performs the actual AHCI copy; the shell only reports which
 * disks exist and forwards the optional target index.
 */
#include "csh.h"

static char g_disk_text[1024];

void csh_disks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    long count = sys_disks(g_disk_text, sizeof(g_disk_text) - 1);
    if (count < 0) {
        csh_error("cannot enumerate disks");
        return;
    }
    g_disk_text[count] = 0;
    csh_out(g_disk_text);
}

static long parse_index(const char *text, int *ok)
{
    long value = 0;
    *ok = 0;
    if (!text || !*text)
        return -1;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        value = value * 10 + (*p - '0');
    }
    *ok = 1;
    return value;
}

void csh_install(int argc, char **argv)
{
    long target = -1;
    int gpt = 0;
    for (int i = 1; i < argc; i++) {
        if (u_strcmp(argv[i], "gpt") == 0) {
            gpt = 1;
            continue;
        }
        if (u_strcmp(argv[i], "mbr") == 0) {
            gpt = 0;
            continue;
        }
        int ok = 0;
        target = parse_index(argv[i], &ok);
        if (!ok) {
            csh_error("usage: install [target-index] [mbr|gpt]");
            return;
        }
    }

    csh_out(gpt ? "Installing GPT + ext4 root; this may take a while...\n"
                : "Installing MBR + ext4 root; this may take a while...\n");
    long result = sys_install(target, gpt);
    if (result == 0) {
        csh_outln("Install complete. Boot the target disk directly.");
        return;
    }

    if (result == -3)
        csh_error("no suitable blank target disk");
    else
        csh_error("install failed while writing the target disk");
}
