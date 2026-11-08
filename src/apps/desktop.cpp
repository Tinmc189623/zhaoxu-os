/*
 * Zhaoxu OS - Desktop-Shell and DS-WM
 *
 * Aqua-flavoured desktop session linked into the kernel image: menu bar,
 * draggable/closable windows with z-order, a dock and a software cursor.
 * Entered from Command-Shell with `desktop [seconds]`, ESC returns.
 */
extern "C" {
#include <boot.h>
#include <console.h>
#include <drivers.h>
#include <install.h>
#include <keyboard.h>
#include <kprintf.h>
#include <rtc.h>
#include <string.h>
#include <timer.h>
#include <ttf.h>
#include <types.h>
#include <version.h>
#include <vfs.h>
}

extern "C" char font8x8_basic[128][8];

namespace {

constexpr int kCellW = 8;
constexpr int kCellH = 16;
constexpr int kMenuH = 26;
constexpr int kTitleH = 24;
constexpr int kMaxWindows = 3;
constexpr int kMaxEntries = 12;

enum window_kind {
    KIND_WELCOME = 0,
    KIND_FILES = 1,
    KIND_INSTALLER = 2,
};

struct window {
    int x, y, w, h;
    const char *title;
    int kind;
    bool open;
    int z;
};

struct file_entry {
    char name[40];
    u64 size;
    int is_dir;
};

u32 *g_fb;
u32 g_pitch_px;
u32 g_width;
u32 g_height;
u32 g_format;
int g_cursor_x = -1;
int g_cursor_y = -1;

window g_windows[kMaxWindows] = {
    {  90, 100, 480, 240, "欢迎 · Welcome", KIND_WELCOME, true, 2 },
    { 600, 150, 440, 330, "文件 · Files", KIND_FILES, true, 1 },
    { 300, 160, 520, 320, "安装 · Installer", KIND_INSTALLER, false, 0 },
};
int g_window_count = 3;
int g_next_z = 3;

enum install_phase {
    INSTALL_IDLE = 0,
    INSTALL_RUNNING = 1,
    INSTALL_DONE = 2,
    INSTALL_FAILED = 3,
};

const window *g_install_win;
int g_install_phase = INSTALL_IDLE;
u32 g_install_pct = 100;
char g_install_message[96];

file_entry g_files[kMaxEntries];
int g_file_count;

int g_drag_window = -1;
int g_drag_dx;
int g_drag_dy;
int g_prev_buttons;

/* The desktop owns the framebuffer; console text would paint over it. */
void serial_line(const char *s)
{
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc((u8)*s++);
    }
}

u32 pack(u32 rgb)
{
    if (g_format == 0)
        return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    return rgb & 0xFFFFFF;
}

void fill_rect(int x, int y, int w, int h, u32 color)
{
    if (w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > (int)g_width)
        x1 = (int)g_width;
    if (y1 > (int)g_height)
        y1 = (int)g_height;
    if (x0 >= x1 || y0 >= y1)
        return;
    u32 pixel = pack(color);
    for (int row = y0; row < y1; row++) {
        u32 *line = g_fb + (u32)row * g_pitch_px + (u32)x0;
        for (int col = x0; col < x1; col++)
            *line++ = pixel;
    }
}

u32 lerp(u32 a, u32 b, int step, int total)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * step / total;
    int g = ag + (bg - ag) * step / total;
    int bl = ab + (bb - ab) * step / total;
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

void vgrad(int x, int y, int w, int h, u32 top, u32 bottom)
{
    if (h <= 0)
        return;
    for (int i = 0; i < h; i++) {
        u32 color = lerp(top, bottom, i, h > 1 ? h - 1 : 1);
        fill_rect(x, y + i, w, 1, color);
    }
}

void border(int x, int y, int w, int h, u32 color)
{
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

int draw_char(u32 codepoint, int x, int y, u32 fg, u32 bg)
{
    if (codepoint < 0x80) {
        if (x >= 0 && y >= 0 && x + kCellW <= (int)g_width &&
            y + kCellH <= (int)g_height)
            gop_blit_glyph((u64)(uintptr_t)g_fb, g_pitch_px * 4, (u32)x,
                           (u32)y, (const u8 *)font8x8_basic[codepoint],
                           pack(fg), pack(bg));
        return kCellW;
    }

    struct ttf_glyph glyph;
    if (ttf_render(codepoint, &glyph) != 0 || glyph.w == 0 || glyph.h == 0)
        return draw_char('?', x, y, fg, bg);

    int cells = glyph.adv > 1 ? glyph.adv : 1;
    int width = cells * kCellW;
    int gx = x + ((width - TTF_PIXEL_SIZE) / 2) + glyph.bx;
    int gy = y + glyph.by;
    int x0 = gx < 0 ? 0 : gx;
    int y0 = gy < 0 ? 0 : gy;
    int x1 = gx + glyph.w;
    int y1 = gy + glyph.h;
    if (x1 > (int)g_width)
        x1 = (int)g_width;
    if (y1 > (int)g_height)
        y1 = (int)g_height;
    if (x1 > x0 && y1 > y0)
        gop_blit_alpha((u64)(uintptr_t)g_fb, g_pitch_px * 4, (u32)x0, (u32)y0,
                       glyph.alpha, glyph.w, (u32)(x0 - gx), (u32)(y0 - gy),
                       (u32)(x1 - x0), (u32)(y1 - y0), pack(fg), pack(bg));
    return width;
}

int draw_text(const char *text, int x, int y, u32 fg, u32 bg)
{
    const u8 *cursor = (const u8 *)text;
    while (*cursor) {
        u32 codepoint = *cursor++;
        if (codepoint >= 0x80) {
            int extra = 0;
            if ((codepoint & 0xE0) == 0xC0) {
                codepoint &= 0x1F;
                extra = 1;
            } else if ((codepoint & 0xF0) == 0xE0) {
                codepoint &= 0x0F;
                extra = 2;
            } else if ((codepoint & 0xF8) == 0xF0) {
                codepoint &= 0x07;
                extra = 3;
            }
            for (int i = 0; i < extra && (*cursor & 0xC0) == 0x80; i++)
                codepoint = (codepoint << 6) | (*cursor++ & 0x3F);
        }
        x += draw_char(codepoint, x, y, fg, bg);
    }
    return x;
}

int text_width(const char *text)
{
    int width = 0;
    const u8 *cursor = (const u8 *)text;
    while (*cursor) {
        u32 codepoint = *cursor++;
        if (codepoint < 0x80) {
            width += kCellW;
            continue;
        }
        int extra = 0;
        if ((codepoint & 0xE0) == 0xC0)
            extra = 1;
        else if ((codepoint & 0xF0) == 0xE0)
            extra = 2;
        else if ((codepoint & 0xF8) == 0xF0)
            extra = 3;
        for (int i = 0; i < extra && (*cursor & 0xC0) == 0x80; i++)
            cursor++;
        struct ttf_glyph glyph;
        if (ttf_render(codepoint, &glyph) == 0 && glyph.adv > 1)
            width += glyph.adv * kCellW;
        else
            width += kCellW;
    }
    return width;
}

void collect_entry(const char *name, u64 size, int is_dir, void *)
{
    if (g_file_count >= kMaxEntries)
        return;
    file_entry *entry = &g_files[g_file_count++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = 0;
    entry->size = size;
    entry->is_dir = is_dir;
}

void load_files(void)
{
    g_file_count = 0;
    vfs_list("/mnt", collect_entry, nullptr);
}

void render_menu_bar()
{
    vgrad(0, 0, (int)g_width, kMenuH, 0xF8F8F8, 0xD2D2D2);
    fill_rect(0, kMenuH - 1, (int)g_width, 1, 0x9AA4AE);

    char left[80];
    ksnprintf(left, sizeof(left), "兆旭 Zhaoxu OS %s", ZHAOXU_VERSION);
    int x = draw_text(left, 12, 5, 0x1C2B3A, 0xE8E8E8);
    draw_text("Desktop-Shell", x + 24, 5, 0x51606F, 0xE8E8E8);
    draw_text("文件", x + 140, 5, 0x51606F, 0xE8E8E8);
    draw_text("编辑", x + 190, 5, 0x51606F, 0xE8E8E8);
    draw_text("帮助", x + 240, 5, 0x51606F, 0xE8E8E8);

    struct rtc_time now;
    char clock[16];
    if (rtc_read(&now) == 0)
        ksnprintf(clock, sizeof(clock), "%02u:%02u:%02u",
                  (unsigned)now.hour, (unsigned)now.minute,
                  (unsigned)now.second);
    else
        ksnprintf(clock, sizeof(clock), "--:--:--");
    draw_text(clock, (int)g_width - text_width(clock) - 14, 5, 0x1C2B3A,
              0xE8E8E8);
}

void render_dock()
{
    int dock_w = 340;
    int dock_h = 58;
    int x = ((int)g_width - dock_w) / 2;
    int y = (int)g_height - dock_h - 12;
    fill_rect(x + 3, y + 4, dock_w, dock_h, 0x22384E);
    vgrad(x, y, dock_w, dock_h, 0xEAF2F9, 0xC9DBEC);
    border(x, y, dock_w, dock_h, 0x7D93A9);

    static const u32 colors[4] = { 0x3B82C4, 0xE9C46A, 0x5AA469, 0xB86FD6 };
    static const char *labels[4] = { "欢迎", "文件", "终端", "安装" };
    for (int i = 0; i < 4; i++) {
        int ix = x + 18 + i * 82;
        int iy = y + 6;
        fill_rect(ix, iy, 38, 32, colors[i]);
        fill_rect(ix, iy, 38, 5, 0xFFFFFF);
        draw_text(labels[i], ix + 3, y + 40, 0x24384E, 0xC9DBEC);
    }
}

void draw_file_row(int index, int x, int y, int body_bg)
{
    const file_entry *entry = &g_files[index];
    fill_rect(x - 2, y - 2, 12, 14, entry->is_dir ? 0xE9C46A : 0x8ECAE6);
    int after = draw_text(entry->name, x + 16, y, 0x24384E, (u32)body_bg);
    if (entry->is_dir)
        return;
    char size[24];
    ksnprintf(size, sizeof(size), "%llu B", entry->size);
    draw_text(size, after + 24, y, 0x667788, (u32)body_bg);
}

void installer_button_rect(const window *win, int *bx, int *by, int *bw,
                           int *bh)
{
    *bx = win->x + 16;
    *by = win->y + win->h - 52;
    *bw = win->w - 32;
    *bh = 34;
}

bool installer_button_at(const window *win, int x, int y)
{
    int bx, by, bw, bh;
    installer_button_rect(win, &bx, &by, &bw, &bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void paint_install_bar(const window *win)
{
    int bx = win->x + 16;
    int by = win->y + win->h - 82;
    int bw = win->w - 32;
    int bh = 18;

    char pct[16];
    ksnprintf(pct, sizeof(pct), "%u%%", (unsigned)g_install_pct);
    draw_text(pct, bx + bw - text_width(pct), by - 22, 0x24384E, 0xF4F6F8);

    fill_rect(bx, by, bw, bh, 0xFFFFFF);
    border(bx, by, bw, bh, 0x8698AC);
    int filled = (int)((u64)(u32)(bw - 2) * g_install_pct / 100);
    if (filled > 0)
        fill_rect(bx + 1, by + 1, filled, bh - 2, 0x3B82C4);
}

void installer_progress(u32 done, u32 total)
{
    g_install_pct = total ? (u32)((u64)done * 100 / total) : 0;
    if (g_install_win)
        paint_install_bar(g_install_win);
}

void start_install(const window *win)
{
    int source = install_source_disk();
    int target = source >= 0 ? install_pick_target(source) : -1;

    g_install_win = win;
    g_install_message[0] = 0;
    if (source < 0) {
        g_install_phase = INSTALL_FAILED;
        ksnprintf(g_install_message, sizeof(g_install_message),
                  "未检测到 ZHAOXUEFI 安装介质。");
        return;
    }
    if (target < 0) {
        g_install_phase = INSTALL_FAILED;
        ksnprintf(g_install_message, sizeof(g_install_message),
                  "没有可用的目标磁盘（需要一块空白非数据盘）。");
        return;
    }

    g_install_phase = INSTALL_RUNNING;
    g_install_pct = 0;
    if (install_run(source, target, installer_progress) == 0) {
        g_install_phase = INSTALL_DONE;
        g_install_pct = 100;
        ksnprintf(g_install_message, sizeof(g_install_message),
                  "安装完成：disk %d 已写入引导扇区与 ESP，重启后可从该盘启动。",
                  target);
    } else {
        g_install_phase = INSTALL_FAILED;
        ksnprintf(g_install_message, sizeof(g_install_message), "安装失败：%s",
                  install_last_error());
    }
}

void render_installer(const window *win)
{
    const u32 body_bg = 0xF4F6F8;
    int x = win->x + 16;
    int y = win->y + kTitleH + 12;

    draw_text("将 Zhaoxu OS 安装到本地磁盘（MBR + FAT16 ESP）。", x, y,
              0x16293C, body_bg);
    y += 26;

    int source = install_source_disk();
    int target = -1;
    char line[96];
    if (source >= 0) {
        target = install_pick_target(source);
        ksnprintf(line, sizeof(line), "安装源：disk %d · ZHAOXUEFI · %llu MiB",
                  source, (install_source_sectors() * 512) >> 20);
        draw_text(line, x, y, 0x2A5C3A, body_bg);
    } else {
        draw_text("安装源：未检测到 ZHAOXUEFI 安装介质", x, y, 0x9A3342,
                  body_bg);
    }
    y += 22;

    if (target >= 0) {
        struct install_disk_info disks[8];
        int count = install_scan(disks, 8);
        const char *model = "";
        u64 sectors = 0;
        for (int i = 0; i < count; i++) {
            if (disks[i].disk != target)
                continue;
            model = disks[i].model;
            sectors = disks[i].sectors;
        }
        ksnprintf(line, sizeof(line), "目标磁盘：disk %d · %s · %llu MiB",
                  target, model, (sectors * 512) >> 20);
        draw_text(line, x, y, 0x1C3A5A, body_bg);
    } else {
        draw_text("目标磁盘：未找到空白磁盘（数据盘与安装介质会被跳过）", x, y,
                  0x9A3342, body_bg);
    }
    y += 26;

    draw_text("注意：安装会覆盖目标磁盘的分区表和数据。", x, y, 0x8A5A20,
              body_bg);

    paint_install_bar(win);

    if (g_install_phase == INSTALL_DONE)
        draw_text(g_install_message, x, win->y + win->h - 128, 0x2A6B3A,
                  body_bg);
    else if (g_install_phase == INSTALL_FAILED)
        draw_text(g_install_message, x, win->y + win->h - 128, 0x9A3342,
                  body_bg);
    else if (g_install_phase == INSTALL_IDLE)
        draw_text("准备就绪。点击下方按钮开始。", x, win->y + win->h - 128,
                  0x51606F, body_bg);

    int bx, by, bw, bh;
    installer_button_rect(win, &bx, &by, &bw, &bh);
    u32 button_color = g_install_phase == INSTALL_RUNNING ? 0x9AA4AE : 0x3B82C4;
    fill_rect(bx, by, bw, bh, button_color);
    fill_rect(bx, by, bw, 4, 0xFFFFFF);
    border(bx, by, bw, bh, 0x2A5C86);
    const char *label = g_install_phase == INSTALL_DONE
                            ? "重新安装 · Reinstall"
                            : g_install_phase == INSTALL_FAILED
                                  ? "重试 · Retry"
                                  : g_install_phase == INSTALL_RUNNING
                                        ? "安装中 · Installing…"
                                        : "开始安装 · Install";
    int lw = text_width(label);
    draw_text(label, bx + (bw - lw) / 2, by + 9, 0xFFFFFF, button_color);
}

void render_window(const window *win)
{
    const u32 body_bg = 0xF4F6F8;

    if (!win->open)
        return;
    fill_rect(win->x + 6, win->y + 6, win->w, win->h, 0x22384E);
    fill_rect(win->x, win->y, win->w, win->h, body_bg);
    border(win->x, win->y, win->w, win->h, 0x8698AC);

    vgrad(win->x + 1, win->y + 1, win->w - 2, kTitleH - 2, 0xFDFEFF,
          0xC3D6E9);
    fill_rect(win->x + 1, win->y + kTitleH - 1, win->w - 2, 1, 0x8698AC);
    draw_text(win->title, win->x + 12, win->y + 4, 0x1C2B3A, 0xDEE9F3);

    int cx = win->x + win->w - 18;
    int cy = win->y + 7;
    fill_rect(cx, cy, 11, 11, 0xE6655A);
    border(cx, cy, 11, 11, 0x8B3B33);
    for (int i = 0; i < 5; i++) {
        fill_rect(cx + 3 + i, cy + 3 + i, 1, 1, 0x6E2620);
        fill_rect(cx + 7 - i, cy + 3 + i, 1, 1, 0x6E2620);
    }

    int body_x = win->x + 14;
    int body_y = win->y + kTitleH + 12;

    if (win->kind == KIND_WELCOME) {
        draw_text("兆旭操作系统 Zhaoxu OS", body_x, body_y, 0x16293C,
                  body_bg);
        char version[80];
        ksnprintf(version, sizeof(version), "%s \"%s\" · 内核 %s Kernel",
                  ZHAOXU_VERSION, ZHAOXU_CODENAME, ZHAOXU_KERNEL);
        draw_text(version, body_x, body_y + 24, 0x3E566E, body_bg);
        fill_rect(body_x, body_y + 46, win->w - 28, 1, 0xD7E0E8);
        draw_text("Desktop-Shell 已启动，DS-WM 正在管理窗口。", body_x,
                  body_y + 58, 0x24384E, body_bg);
        draw_text("· 拖动标题栏移动窗口，点红色按钮关闭", body_x,
                  body_y + 82, 0x24384E, body_bg);
        draw_text("· 点击 Dock 图标可重新打开窗口", body_x, body_y + 106,
                  0x24384E, body_bg);
        draw_text("· 按 ESC 返回 Command-Shell", body_x, body_y + 130,
                  0x8A3342, body_bg);
        if (ttf_available())
            draw_text("中文渲染：文泉驿微米黑 16px 矢量字形", body_x,
                      body_y + 154, 0x51606F, body_bg);
        else
            draw_text("未加载中文字体，CJK 显示为 ?（挂载数据盘可恢复）",
                      body_x, body_y + 154, 0x9A5B20, body_bg);
    } else if (win->kind == KIND_FILES) {
        if (g_file_count == 0) {
            draw_text("/mnt 为空或不可用", body_x, body_y, 0x51606F,
                      body_bg);
        } else {
            for (int i = 0; i < g_file_count; i++) {
                int row_y = body_y + i * 22;
                if (row_y + kCellH > win->y + win->h - 8)
                    break;
                draw_file_row(i, body_x, row_y, (int)body_bg);
            }
        }
    } else {
        render_installer(win);
    }
}

void render_cursor(int x, int y)
{
    static const char *shape[19] = {
        "X           ",
        "XX          ",
        "XoX         ",
        "XooX        ",
        "XoooX       ",
        "XooooX      ",
        "XoooooX     ",
        "XooooooX    ",
        "XoooooooX   ",
        "XooooooooX  ",
        "XoooooXXXXX ",
        "XooXooX     ",
        "XoX XooX    ",
        "XX  XooX    ",
        "X    XooX   ",
        "     XooX   ",
        "      XooX  ",
        "      XooX  ",
        "       XX   ",
    };
    for (int row = 0; row < 19; row++) {
        const char *line = shape[row];
        for (int col = 0; line[col]; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || py < 0 || px >= (int)g_width || py >= (int)g_height)
                continue;
            if (line[col] == 'X')
                g_fb[(u32)py * g_pitch_px + (u32)px] = pack(0x000000);
            else if (line[col] == 'o')
                g_fb[(u32)py * g_pitch_px + (u32)px] = pack(0xFFFFFF);
        }
    }
}

void render(int mouse_x, int mouse_y)
{
    if (g_height > (u32)kMenuH)
        vgrad(0, kMenuH, (int)g_width, (int)g_height - kMenuH, 0x1C3A5A,
              0x86B7DF);
    render_menu_bar();
    render_dock();

    for (int z = 1; z <= g_next_z; z++) {
        for (int i = 0; i < g_window_count; i++) {
            if (g_windows[i].open && g_windows[i].z == z)
                render_window(&g_windows[i]);
        }
    }

    render_cursor(mouse_x, mouse_y);
    g_cursor_x = mouse_x;
    g_cursor_y = mouse_y;
}

int window_index_at(int x, int y)
{
    int best = -1;
    int best_z = 0;
    for (int i = 0; i < g_window_count; i++) {
        const window *win = &g_windows[i];
        if (!win->open)
            continue;
        if (x < win->x || y < win->y || x >= win->x + win->w ||
            y >= win->y + win->h)
            continue;
        if (win->z > best_z) {
            best = i;
            best_z = win->z;
        }
    }
    return best;
}

bool close_button_at(const window *win, int x, int y)
{
    int cx = win->x + win->w - 18;
    int cy = win->y + 7;
    return x >= cx - 2 && x <= cx + 14 && y >= cy - 2 && y <= cy + 14;
}

void bring_to_front(int index)
{
    g_windows[index].z = ++g_next_z;
    if (g_next_z > 1000) {
        for (int i = 0; i < g_window_count; i++)
            g_windows[i].z /= 2;
        g_next_z /= 2;
    }
}

void dock_click(int x, int y)
{
    int dock_w = 340;
    int dock_h = 58;
    int dx = ((int)g_width - dock_w) / 2;
    int dy = (int)g_height - dock_h - 12;
    if (x < dx || y < dy || x >= dx + dock_w || y >= dy + dock_h)
        return;

    int index = (x - dx - 18) / 82;
    int kind;
    if (index == 0)
        kind = KIND_WELCOME;
    else if (index == 1)
        kind = KIND_FILES;
    else if (index == 3)
        kind = KIND_INSTALLER;
    else
        return;     /* terminal arrives with a later milestone */

    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].kind != kind)
            continue;
        g_windows[i].open = true;
        bring_to_front(i);
        if (kind == KIND_FILES)
            load_files();
        return;
    }
}

int parse_seconds(int argc, char **argv)
{
    if (argc < 2)
        return 0;
    int value = 0;
    for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
        value = value * 10 + (*p - '0');
    return value;
}

} // namespace

int desktop_run(int argc, char **argv)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot->framebuffer_base) {
        kprintf("[desk] no framebuffer, Desktop-Shell needs GOP\n");
        return -1;
    }

    g_fb = (u32 *)(uintptr_t)boot->framebuffer_base;
    g_pitch_px = boot->pitch / 4;
    g_width = boot->width;
    g_height = boot->height;
    g_format = boot->pixel_format;

    g_drag_window = -1;
    g_prev_buttons = 0;
    g_install_win = nullptr;
    g_install_phase = INSTALL_IDLE;
    g_install_pct = 100;
    g_install_message[0] = 0;
    load_files();

    int seconds = parse_seconds(argc, argv);
    u64 deadline = timer_ticks() + (u64)seconds * timer_hz();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "install") != 0)
            continue;
        g_windows[2].open = true;
        bring_to_front(2);
    }

    render(mouse_x(), mouse_y());
    serial_line("[desk] Desktop-Shell active, ESC exits\n");

    for (;;) {
        int key = console_read_char();
        if (key == 27 || key == 3)
            break;
        if (seconds > 0 && timer_ticks() >= deadline)
            break;

        bool changed = false;
        int mx = mouse_x();
        int my = mouse_y();
        int buttons = mouse_buttons();
        bool pressed = (buttons & 1) && !(g_prev_buttons & 1);

        if (pressed) {
            int index = window_index_at(mx, my);
            if (index >= 0) {
                window *win = &g_windows[index];
                if (win->kind == KIND_INSTALLER &&
                    g_install_phase != INSTALL_RUNNING &&
                    installer_button_at(win, mx, my)) {
                    start_install(win);
                } else if (close_button_at(win, mx, my)) {
                    win->open = false;
                    g_drag_window = -1;
                } else if (my < win->y + kTitleH) {
                    bring_to_front(index);
                    g_drag_window = index;
                    g_drag_dx = mx - win->x;
                    g_drag_dy = my - win->y;
                } else {
                    bring_to_front(index);
                }
                changed = true;
            } else if (my >= kMenuH) {
                dock_click(mx, my);
                changed = true;
            }
        }

        if (g_drag_window >= 0 && (buttons & 1)) {
            window *win = &g_windows[g_drag_window];
            int nx = mx - g_drag_dx;
            int ny = my - g_drag_dy;
            if (nx < -win->w + 80)
                nx = -win->w + 80;
            if (nx > (int)g_width - 80)
                nx = (int)g_width - 80;
            if (ny < kMenuH)
                ny = kMenuH;
            if (ny > (int)g_height - 30)
                ny = (int)g_height - 30;
            if (nx != win->x || ny != win->y) {
                win->x = nx;
                win->y = ny;
                changed = true;
            }
        }
        if (!(buttons & 1))
            g_drag_window = -1;

        if (mx != g_cursor_x || my != g_cursor_y)
            changed = true;

        g_prev_buttons = buttons;
        if (changed)
            render(mx, my);

        timer_sleep_ms(10);
    }

    console_clear();
    kprintf("Desktop-Shell closed, Command-Shell restored.\n");
    serial_line("[desk] Desktop-Shell closed\n");
    return 0;
}
