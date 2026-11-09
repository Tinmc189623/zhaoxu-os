/* Zhaoxu OS - Files application (outside ADE, uses the desktop API). */
extern "C" {
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>
}

#include "../../ADE/ade.h"

namespace {

constexpr int kMaxEntries = 32;
constexpr int kPreviewSize = 4096;

struct file_entry {
    char name[48];
    u64 size;
    int is_dir;
};

file_entry g_entries[kMaxEntries];
int g_entry_count;
int g_selected = -1;
int g_preview_mode;
char g_preview[kPreviewSize];
int g_preview_len;

void collect(const char *name, u64 size, int is_dir, void *)
{
    if (g_entry_count >= kMaxEntries || !name)
        return;
    file_entry *entry = &g_entries[g_entry_count++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = 0;
    entry->size = size;
    entry->is_dir = is_dir;
}

void load(void)
{
    g_entry_count = 0;
    g_selected = -1;
    g_preview_mode = 0;
    vfs_list("/mnt", collect, nullptr);
}

int header_y(const struct ade_view *view)
{
    return view->y + ade_ui(12);
}

int rows_y(const struct ade_view *view)
{
    return view->y + ade_ui(52);
}

int row_height(void)
{
    return ade_line_height() + ade_ui(8);
}

void paint_list(const struct ade_view *view)
{
    int x = view->x + ade_ui(16);
    int y = rows_y(view);

    if (g_entry_count == 0) {
        ade_text("/mnt 为空或没有挂载数据盘。", x, y, 0x9A5B20, 1);
        return;
    }

    for (int i = 0; i < g_entry_count; i++) {
        if (y + row_height() > view->y + view->h - ade_ui(10))
            break;
        const file_entry *entry = &g_entries[i];
        if (i == g_selected)
            ade_round(x - ade_ui(4), y - ade_ui(3),
                      view->w - ade_ui(24), row_height(), ade_ui(6),
                      0xD9ECF9);
        u32 chip = entry->is_dir ? 0xE9A23B : 0x8ECAE6;
        ade_round(x, y + ade_ui(3), ade_ui(16), ade_ui(16), ade_ui(4), chip);
        ade_text(entry->name, x + ade_ui(24), y, 0x24384E, 1);
        if (!entry->is_dir) {
            char size[24];
            ksnprintf(size, sizeof(size), "%llu B", entry->size);
            int width = ade_text_width(size, 1);
            ade_text(size, view->x + view->w - ade_ui(18) - width, y,
                     0x6B7F93, 1);
        }
        y += row_height();
    }
}

void paint_preview(const struct ade_view *view)
{
    int x = view->x + ade_ui(16);
    int y = rows_y(view);
    int line = ade_line_height();
    int column = 0;
    char text[8];

    for (int i = 0; i < g_preview_len &&
                    y + line <= view->y + view->h - ade_ui(12);) {
        unsigned char lead = (unsigned char)g_preview[i];
        int bytes = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3
                                : lead >= 0xC0 ? 2 : 1;
        if (i + bytes > g_preview_len)
            bytes = 1;
        if (lead == '\n') {
            x = view->x + ade_ui(16);
            y += line;
            column = 0;
            i++;
            continue;
        }
        if (column >= (view->w - ade_ui(40)) / 9) {
            x = view->x + ade_ui(16);
            y += line;
            column = 0;
        }
        memset(text, 0, sizeof(text));
        for (int b = 0; b < bytes; b++)
            text[b] = g_preview[i + b];
        x += ade_text(text, x, y, 0x24384E, 1);
        column++;
        i += bytes;
    }
}

void paint(const struct ade_view *view)
{
    int x = view->x + ade_ui(16);
    int y = header_y(view);

    ade_text(g_preview_mode ? "预览" : "卷 /mnt · FAT32 只读", x, y,
             0x5A6E82, 1);
    ade_text("刷新", view->x + view->w - ade_ui(54), y, 0x2B6FA8, 1);
    ade_hline(x, y + ade_line_height() + ade_ui(6),
              view->w - ade_ui(32), 0xD5E0EA);

    if (g_preview_mode)
        paint_preview(view);
    else
        paint_list(view);
}

void click(const struct ade_view *view, int x, int y, int buttons)
{
    (void)buttons;
    if (g_preview_mode) {
        g_preview_mode = 0;
        ade_invalidate();
        return;
    }

    if (y < rows_y(view) + ade_ui(4)) {
        if (x > view->x + view->w - ade_ui(90)) {
            load();
            ade_invalidate();
        }
        return;
    }

    int row = (y - rows_y(view)) / row_height();
    if (row < 0 || row >= g_entry_count)
        return;
    const file_entry *entry = &g_entries[row];
    if (entry->is_dir)
        return;

    char path[96];
    ksnprintf(path, sizeof(path), "/mnt/%s", entry->name);
    u8 *data = nullptr;
    u64 size = 0;
    if (vfs_read_all(path, &data, &size) != 0)
        return;

    int copy = (int)(size < kPreviewSize - 1 ? size : kPreviewSize - 1);
    for (int i = 0; i < copy; i++)
        g_preview[i] = (char)data[i];
    g_preview[copy] = 0;
    g_preview_len = copy;
    g_preview_mode = 1;
    g_selected = row;
    kfree(data);
    ade_invalidate();
}

void key(int code)
{
    if (code == 27 && g_preview_mode) {
        g_preview_mode = 0;
        ade_invalidate();
    }
}

void init(void)
{
    load();
}

} // namespace

extern const struct ade_app files_app = {
    "files", "文件", "F",
    "<svg viewBox=\"0 0 100 100\"><rect x=\"6\" y=\"6\" width=\"88\" "
    "height=\"88\" rx=\"22\" fill=\"#E9A23B\"/><path d=\"M22 34 L42 34 "
    "L48 41 L78 41 L78 72 L22 72 Z\" fill=\"#FFFFFF\"/></svg>",
    0xE9A23B, 620, 480,
    init, paint, key, click, nullptr,
};
