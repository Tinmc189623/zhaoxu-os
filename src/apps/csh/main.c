/*
 * csh - line editor and command dispatcher.
 *
 * The shell runs in ring 3 and only talks to the kernel through the ZOS
 * API; command implementations live in builtins.c, files.c and run.c.
 */
#include <version.h>

#include "csh.h"

static char g_line[CSH_MAX_LINE];
static int g_length;

static void prompt(void)
{
    csh_out("8sh$ ");
}

static void echo_char(long c)
{
    char text[2];
    text[0] = (char)c;
    text[1] = 0;
    csh_out(text);
}

static void read_line(void)
{
    g_length = 0;
    for (;;) {
        long c = sys_key();
        if (c < 0) {
            sys_sleep(5);
            continue;
        }
        if (c == '\r' || c == '\n') {
            csh_out("\n");
            g_line[g_length] = 0;
            return;
        }
        if (c == 3) {                       /* Ctrl+C */
            csh_out("^C\n");
            g_length = 0;
            g_line[0] = 0;
            return;
        }
        if (c == 8 || c == 127) {
            if (g_length > 0) {
                g_length--;
                csh_out("\b \b");
            }
            continue;
        }
        if (c >= 32 && c < 127 && g_length + 1 < CSH_MAX_LINE) {
            g_line[g_length++] = (char)c;
            echo_char(c);
        }
    }
}

static int tokenize(char *line, char **argv, int maximum)
{
    int argc = 0;
    char *cursor = line;

    while (*cursor && argc < maximum) {
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (!*cursor)
            break;
        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t')
            cursor++;
        if (*cursor)
            *cursor++ = 0;
    }
    return argc;
}

static void execute(char *line)
{
    char *argv[CSH_MAX_ARGS];
    int argc = tokenize(line, argv, CSH_MAX_ARGS);
    if (argc == 0)
        return;

    for (int i = 0; i < csh_command_count; i++) {
        if (u_strcmp(argv[0], csh_commands[i].name) == 0) {
            csh_commands[i].handler(argc, argv);
            return;
        }
    }

    csh_out(argv[0]);
    csh_outln(": command not found. Try 'help'.");
}

int user_main(long *stack)
{
    (void)stack;

    csh_out("8sh - Zhaoxu OS " ZHAOXU_VERSION " build " ZHAOXU_BUILD
            " (" ZHAOXU_CODENAME ")\n");
    csh_outln("Type 'help' for the command list.");

    for (;;) {
        prompt();
        read_line();
        execute(g_line);
    }
}
