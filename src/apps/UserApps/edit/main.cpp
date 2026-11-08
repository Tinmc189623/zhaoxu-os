/*
 * Zhaoxu OS - in-OS source editor.
 *
 * Keeps a small line buffer in the Command-Shell session and drives the
 * zcc compiler (apps/zcc.cpp) and the ring 3 loader, so programs can be
 * written, compiled and run entirely on the machine.
 */
extern "C" {
#include <console.h>
#include <kprintf.h>
#include <process.h>
#include <string.h>
#include <types.h>
}

extern "C" int zcc_compile(const char *source, const u8 **image, u64 *size,
                           const char **error);

namespace {

constexpr usize kEditorCapacity = 4096;

char g_buffer[kEditorCapacity];
usize g_length;
bool g_active;

const char *const kSample =
    "int main() {\n"
    "    print(\"Hello from zcc inside Zhaoxu OS!\\n\");\n"
    "    int i = 1;\n"
    "    while (i <= 5) {\n"
    "        printnum(i);\n"
    "        print(\" squared = \");\n"
    "        printnum(i * i);\n"
    "        print(\"\\n\");\n"
    "        i = i + 1;\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

void error(const char *message)
{
    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("%s\n", message);
    console_reset_color();
}

void help()
{
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("editor commands (a line starting with '.'):\n");
    kprintf("  .list   show the buffer with line numbers\n");
    kprintf("  .run    compile the buffer with zcc and run it\n");
    kprintf("  .clear  empty the buffer\n");
    kprintf("  .end    leave the editor, keep the buffer\n");
    kprintf("  .quit   leave the editor, discard the buffer\n");
    kprintf("  .help   this list\n");
    kprintf("language: int main() { int x; x = e; if (e) s; while (e) s;\n");
    kprintf("          print(\"text\"); printnum(e); return e; }\n");
    console_reset_color();
}

void append(const char *text)
{
    usize length = strlen(text);
    if (g_length + length + 1 >= kEditorCapacity) {
        error("edit: buffer full (4096 bytes)");
        return;
    }
    memcpy(g_buffer + g_length, text, length);
    g_length += length;
    g_buffer[g_length++] = '\n';
    g_buffer[g_length] = 0;
}

void list()
{
    console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
    int number = 1;
    usize start = 0;
    for (usize i = 0; i <= g_length; i++) {
        if (i != g_length && g_buffer[i] != '\n')
            continue;
        char line[120];
        usize length = i - start;
        if (length > sizeof(line) - 1)
            length = sizeof(line) - 1;
        memcpy(line, g_buffer + start, length);
        line[length] = 0;
        kprintf("%3d  %s\n", number, line);
        number++;
        start = i + 1;
    }
    console_reset_color();
}

void compile_and_run()
{
    if (g_length == 0) {
        error("zcc: the editor buffer is empty; run 'edit' first");
        return;
    }

    const u8 *image = nullptr;
    u64 size = 0;
    const char *message = nullptr;
    if (zcc_compile(g_buffer, &image, &size, &message) != 0) {
        console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        kprintf("zcc: %s\n", message ? message : "compile failed");
        console_reset_color();
        return;
    }

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("zcc: %llu bytes of .zxe, running in ring 3...\n", size);
    console_reset_color();
    int code = process_run_image(image, size, "<zcc>", 0, nullptr);
    kprintf("[zcc] exit code %d\n", code);
}

} // namespace

extern "C" bool zx_edit_active(void)
{
    return g_active;
}

extern "C" void zx_edit_feed_line(const char *line)
{
    if (strcmp(line, ".end") == 0) {
        g_active = false;
        kprintf("edit: buffer kept (%llu bytes). 'cc' or 'edit' to continue.\n",
                (unsigned long long)g_length);
        return;
    }
    if (strcmp(line, ".quit") == 0) {
        g_active = false;
        g_length = 0;
        g_buffer[0] = 0;
        kprintf("edit: buffer discarded.\n");
        return;
    }
    if (strcmp(line, ".clear") == 0) {
        g_length = 0;
        g_buffer[0] = 0;
        kprintf("edit: buffer cleared.\n");
        return;
    }
    if (strcmp(line, ".list") == 0) {
        list();
        return;
    }
    if (strcmp(line, ".run") == 0) {
        compile_and_run();
        return;
    }
    if (strcmp(line, ".help") == 0) {
        help();
        return;
    }
    if (line[0] == '.') {
        console_set_color(COLOR_YELLOW, COLOR_BLACK);
        kprintf("edit: unknown command '%s' (.help lists them)\n", line);
        console_reset_color();
        return;
    }
    append(line);
}

extern "C" void zx_cmd_edit(int argc, char **argv)
{
    if (g_length == 0)
        append(kSample);

    g_active = true;
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("edit: %s\n", argc > 1 ? argv[1] : "untitled.zc");
    console_reset_color();
    kprintf("Type C statements, '.end' keeps the buffer, '.help' lists commands.\n");
    if (g_length)
        list();
}

extern "C" void zx_cmd_cc(int, char **)
{
    if (g_length == 0) {
        error("cc: no source in the editor buffer; run 'edit' first");
        return;
    }
    compile_and_run();
}
