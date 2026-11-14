/*
 * Zhaoxu OS - PNG + text cursor theme loader.
 *
 * The Desktop Window Compositor owns the cursor pixels, so the theme is
 * loaded here rather than in the kernel's hardcoded fallback cursor.  Text
 * parsing is deliberately small: section names, `image`/`frames`, `hotspot`
 * and `delay`.  Every frame is decoded to 0xAARRGGBB and kept in a fixed
 * pool, which keeps the compositor allocation-free while it moves.
 */
#include <kprintf.h>
#include <string.h>
#include <types.h>
#include <userlib.h>

#include "cursor.h"

#define THEME_FILE_MAX 8192
#define PNG_FILE_MAX (256u * 1024u)

static struct cursor_def g_defs[CURSOR_MAX_DEFS];
static int g_def_count;
static u32 g_pixels[CURSOR_MAX_FRAME_POOL]
                   [CURSOR_MAX_SIZE * CURSOR_MAX_SIZE];
static int g_frame_pool;
static char g_theme_name[CURSOR_NAME_MAX];
static char g_theme_dir[192];
static char g_text[THEME_FILE_MAX];
static u8 g_png[PNG_FILE_MAX];

/* Small decimal parser; the ADE binary does not link the kernel ABI's
 * atoi implementation. */
static int parse_int(const char *text)
{
    int sign = 1;
    int value = 0;
    if (!text)
        return 0;
    while (*text == ' ' || *text == '\t')
        text++;
    if (*text == '-' || *text == '+') {
        if (*text == '-')
            sign = -1;
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value * sign;
}

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r')
        text++;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return text;
}

static struct cursor_def *find_or_create(const char *name)
{
    for (int i = 0; i < g_def_count; i++)
        if (strcmp(g_defs[i].name, name) == 0)
            return &g_defs[i];
    if (g_def_count >= CURSOR_MAX_DEFS)
        return NULL;
    struct cursor_def *definition = &g_defs[g_def_count++];
    memset(definition, 0, sizeof(*definition));
    strncpy(definition->name, name, sizeof(definition->name) - 1);
    definition->delay_ms = 100;
    return definition;
}

/* Load one PNG frame from a file name relative to the theme directory. */
static void load_frame(struct cursor_def *definition, const char *file)
{
    if (!definition || definition->frame_count >= CURSOR_MAX_FRAMES ||
        g_frame_pool >= CURSOR_MAX_FRAME_POOL)
        return;
    if (!file[0])
        return;
    char path[256];
    ksnprintf(path, sizeof(path), "%s/%s", g_theme_dir, file);

    long length = sys_read_path(path, g_png, sizeof(g_png));
    if (length <= 0)
        return;
    int width = 0;
    int height = 0;
    u32 *pixels = g_pixels[g_frame_pool];
    int count = cursor_decode_png(g_png, (usize)length, &width, &height,
                                  pixels,
                                  CURSOR_MAX_SIZE * CURSOR_MAX_SIZE);
    if (count <= 0 || width > CURSOR_MAX_SIZE || height > CURSOR_MAX_SIZE)
        return;
    struct cursor_frame *frame = &definition->frames[definition->frame_count++];
    frame->pixels = pixels;
    frame->width = width;
    frame->height = height;
    g_frame_pool++;
}

static void add_frame_list(struct cursor_def *definition, char *list)
{
    char *cursor = list;
    while (cursor && *cursor) {
        char *comma = strchr(cursor, ',');
        if (comma)
            *comma = 0;
        char *name = trim(cursor);
        if (*name)
            load_frame(definition, name);
        cursor = comma ? comma + 1 : NULL;
    }
}

/* Parse the text descriptor; frames are decoded as their names appear. */
static int parse_theme(const char *directory)
{
    char path[256];
    ksnprintf(path, sizeof(path), "%s/cursor.theme", directory);
    long length = sys_read_path(path, g_text, sizeof(g_text) - 1);
    if (length <= 0)
        return -1;
    g_text[length] = 0;

    struct cursor_def *active = NULL;
    char *line = g_text;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *text = trim(line);
        if (*text && *text != '#') {
            if (*text == '[') {
                char *close = strchr(text, ']');
                if (close)
                    *close = 0;
                char *section = trim(text + 1);
                if (strcmp(section, "theme") == 0) {
                    active = NULL;
                } else {
                    active = find_or_create(section);
                }
            } else {
                char *equals = strchr(text, '=');
                if (equals && active) {
                    *equals = 0;
                    char *key = trim(text);
                    char *value = trim(equals + 1);
                    if (strcmp(key, "image") == 0) {
                        load_frame(active, value);
                    } else if (strcmp(key, "frames") == 0) {
                        add_frame_list(active, value);
                    } else if (strcmp(key, "hotspot") == 0) {
                        char *comma = strchr(value, ',');
                        if (comma) {
                            *comma = 0;
                            active->hotspot_x = parse_int(trim(value));
                            active->hotspot_y = parse_int(trim(comma + 1));
                        }
                    } else if (strcmp(key, "delay") == 0) {
                        active->delay_ms = parse_int(value);
                        if (active->delay_ms <= 0)
                            active->delay_ms = 100;
                    }
                } else if (equals && !active) {
                    *equals = 0;
                    char *key = trim(text);
                    char *value = trim(equals + 1);
                    if (strcmp(key, "name") == 0)
                        strncpy(g_theme_name, value,
                                sizeof(g_theme_name) - 1);
                }
            }
        }
        line = next;
    }
    return g_def_count ? 0 : -1;
}

int cursor_theme_load(const char *directory)
{
    if (!directory)
        return -1;
    g_def_count = 0;
    g_frame_pool = 0;
    g_theme_name[0] = 0;
    strncpy(g_theme_dir, directory, sizeof(g_theme_dir) - 1);
    g_theme_dir[sizeof(g_theme_dir) - 1] = 0;
    memset(g_defs, 0, sizeof(g_defs));
    if (parse_theme(directory) != 0)
        return -1;
    /* Drop empty sections that only appeared as [theme] children. */
    int kept = 0;
    for (int i = 0; i < g_def_count; i++) {
        if (g_defs[i].frame_count)
            g_defs[kept++] = g_defs[i];
    }
    g_def_count = kept;
    return g_def_count ? 0 : -1;
}

const struct cursor_def *cursor_find(const char *name)
{
    if (name)
        for (int i = 0; i < g_def_count; i++)
            if (strcmp(g_defs[i].name, name) == 0)
                return &g_defs[i];
    for (int i = 0; i < g_def_count; i++)
        if (strcmp(g_defs[i].name, "arrow") == 0)
            return &g_defs[i];
    return g_def_count ? &g_defs[0] : NULL;
}

int cursor_theme_ready(void)
{
    return g_def_count > 0;
}

int cursor_theme_count(void)
{
    return g_def_count;
}

const char *cursor_theme_name(void)
{
    return g_theme_name[0] ? g_theme_name : "unknown";
}

/* Choose the animation frame for the current uptime. */
int cursor_frame_index(const struct cursor_def *definition, u64 now_ms)
{
    if (!definition || definition->frame_count <= 1)
        return 0;
    int delay = definition->delay_ms > 0 ? definition->delay_ms : 100;
    return (int)((now_ms / (u64)delay) %
                 (u64)definition->frame_count);
}
