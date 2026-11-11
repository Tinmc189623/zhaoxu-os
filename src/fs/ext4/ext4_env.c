#include "ext4_env.h"
#include <kprintf.h>
#include <string.h>

static struct ext4_env g_env;

void ext4_env_set(const struct ext4_env *env)
{
    g_env = *env;
}

const struct ext4_env *ext4_env(void)
{
    return &g_env;
}

void *ext4_alloc(u32 bytes)
{
    return g_env.alloc ? g_env.alloc(bytes) : 0;
}

void ext4_free(void *pointer)
{
    if (g_env.free)
        g_env.free(pointer);
}

void ext4_logf(const char *format, ...)
{
    if (!g_env.log)
        return;
    char text[160];
    __builtin_va_list args;
    __builtin_va_start(args, format);
    kvsnprintf(text, sizeof(text), format, args);
    __builtin_va_end(args);
    g_env.log(text);
}
