/*
 * Zhaoxu OS - graphical installer client for the Ring3 ADE server.
 *
 * The flow is deliberately short: pick a blank target disk (auto-selected
 * when there is only one), confirm that its data will be erased, and wait
 * for the kernel installer to finish.  MBR/GPT is an advanced toggle; GPT is
 * the default because the BIOS chain also boots the GPT layout.
 */
#include <ade_client.h>
#include <ade_protocol.h>
#include <userlib.h>

#define WINDOW_W 620
/* The ADE window height includes the title bar and border; leave enough
 * client height for the 402-pixel select screen. */
#define WINDOW_H 480
#define MAX_TARGETS 8
#define VISIBLE_TARGETS 4

#define COLOR_BLUE 0x1E6FB8
#define COLOR_DARK 0x1B2C3D
#define COLOR_GRAY 0x51606F
#define COLOR_PANEL 0xF7FAFD
#define COLOR_ROW 0xE8F1F9
#define COLOR_ROW_SELECTED 0xD6E9FA
#define COLOR_GREEN 0x2E7D32
#define COLOR_RED 0xC62828

struct target_disk {
    int index;
    unsigned long mib;
    char model[64];
};

enum install_state {
    STATE_SELECT = 0,
    STATE_CONFIRM,
    STATE_INSTALLING,
    STATE_DONE,
    STATE_FAILED
};

static struct target_disk g_targets[MAX_TARGETS];
static char g_disks[1024];
static int g_target_count;
static int g_selected;
static int g_gpt = 1;
static int g_advanced;
static enum install_state g_state;
static char g_error[160];
static struct zos_install_progress *g_progress;
static long g_progress_id;
static long g_worker_pid;

static unsigned long parse_uint(const char *text, const char **end_out)
{
    unsigned long value = 0;
    while (*text == ' ')
        text++;
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (unsigned long)(*text - '0');
        text++;
    }
    if (end_out)
        *end_out = text;
    return value;
}

static void copy_text(char *destination, int capacity, const char *source,
                      int length)
{
    int i = 0;
    for (; i < length && i + 1 < capacity && source[i]; i++)
        destination[i] = source[i];
    destination[i] = 0;
}

/* Parse the text produced by sys_disks().  Each data line is
 * "Idx Port Size MiB Label Role Model"; the role is the sixth field and the
 * model (which may contain spaces) starts at the seventh.  Only "target"
 * disks are offered as install destinations. */
static void parse_targets(const char *text)
{
    g_target_count = 0;
    const char *line = text;
    while (*line && g_target_count < MAX_TARGETS) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;

        const char *field[8];
        int count = 0;
        const char *p = line;
        while (p < end && count < 8) {
            while (p < end && *p == ' ')
                p++;
            if (p >= end)
                break;
            field[count++] = p;
            while (p < end && *p != ' ')
                p++;
        }
        if (count >= 6) {
            const char *role = field[5];
            const char *expect = "target";
            int matched = 0;
            while (matched < 6 && role + matched < end &&
                   role[matched] == expect[matched])
                matched++;
            int role_is_target =
                matched == 6 &&
                (role + 6 == end || role[6] == ' ');
            if (role_is_target) {
                struct target_disk *target = &g_targets[g_target_count++];
                target->index = (int)parse_uint(field[0], 0);
                target->mib = parse_uint(field[2], 0);
                if (count >= 7) {
                    const char *model = field[6];
                    int length = (int)(end - model);
                    while (length > 0 && (model[length - 1] == '\r' ||
                                          model[length - 1] == ' '))
                        length--;
                    copy_text(target->model, sizeof(target->model), model,
                              length);
                }
                if (!target->model[0])
                    copy_text(target->model, sizeof(target->model), "disk", 4);
            }
        }
        line = *end ? end + 1 : end;
    }
    if (g_selected >= g_target_count)
        g_selected = 0;
}

/* Re-read the block device table and rebuild the target list. */
static int refresh_disks(void)
{
    long count = sys_disks(g_disks, sizeof(g_disks) - 1);
    if (count <= 0) {
        g_target_count = 0;
        return 0;
    }
    if (count >= (long)sizeof(g_disks))
        count = (long)sizeof(g_disks) - 1;
    g_disks[count] = 0;
    parse_targets(g_disks);
    return g_target_count;
}

static int text_length(const char *text)
{
    int length = 0;
    while (text[length])
        length++;
    return length;
}

static void append_text(char *buffer, int capacity, int *at, const char *text)
{
    while (*text && *at + 1 < capacity)
        buffer[(*at)++] = *text++;
    buffer[*at] = 0;
}

static void append_uint(char *buffer, int capacity, int *at,
                        unsigned long value)
{
    char digits[24];
    int count = 0;
    if (value == 0)
        digits[count++] = '0';
    while (value) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count > 0 && *at + 1 < capacity)
        buffer[(*at)++] = digits[--count];
    buffer[*at] = 0;
}

static void target_label(char *buffer, int capacity, int index)
{
    int at = 0;
    buffer[0] = 0;
    append_text(buffer, capacity, &at, "磁盘 ");
    append_uint(buffer, capacity, &at,
                (unsigned long)(g_targets[index].index + 1));
    append_text(buffer, capacity, &at, "  ");
    append_uint(buffer, capacity, &at, g_targets[index].mib);
    append_text(buffer, capacity, &at, " MB  ");
    append_text(buffer, capacity, &at, g_targets[index].model);
}

static void draw_button(long channel, int x, int y, int w, int h, u32 color,
                        const char *text)
{
    ade_round(channel, x, y, w, h, 10, color);
    int width = text_length(text) * 7;
    ade_text(channel, x + (w - width) / 2, y + 16, 0xFFFFFF, text);
}

static int target_row_y(int index)
{
    return 124 + index * 44;
}

static void draw_select(long channel)
{
    ade_clear(channel, COLOR_PANEL);
    ade_fill(channel, 0, 0, WINDOW_W, 72, COLOR_BLUE);
    ade_text(channel, 24, 16, 0xFFFFFF, "安装 Zhaoxu OS");
    ade_text(channel, 24, 44, 0xD9ECFF, "单 ISO 安装，无需额外介质");

    ade_text(channel, 24, 92, COLOR_DARK, "目标磁盘");
    if (g_target_count == 0) {
        ade_text(channel, 24, 124, COLOR_RED,
                 "未找到可安装的空白磁盘。请连接一块至少 1 GB 的磁盘。");
    } else {
        int visible = g_target_count < VISIBLE_TARGETS ? g_target_count
                                                       : VISIBLE_TARGETS;
        for (int i = 0; i < visible; i++) {
            int y = target_row_y(i);
            ade_round(channel, 24, y, 572, 38, 8,
                      i == g_selected ? COLOR_ROW_SELECTED : COLOR_ROW);
            char label[128];
            target_label(label, sizeof(label), i);
            ade_text(channel, 38, y + 10, COLOR_DARK, label);
            if (i == g_selected)
                ade_cursor(channel, 24, y, 572, 38, "hand");
        }
        if (g_target_count > visible)
            ade_text(channel, 24, target_row_y(visible), COLOR_GRAY,
                     "还有更多磁盘，请断开多余磁盘后重试。");
    }

    ade_text(channel, 24, 318, COLOR_DARK, "分区方案");
    ade_text(channel, 130, 318, COLOR_GRAY,
             g_gpt ? "自动 · GPT（推荐）" : "自动 · MBR（兼容）");
    if (g_advanced) {
        ade_round(channel, 430, 306, 166, 34, 8, 0xDCE6EF);
        ade_text(channel, 452, 315, COLOR_DARK,
                 g_gpt ? "切换为 MBR" : "切换为 GPT");
        ade_cursor(channel, 430, 306, 166, 34, "hand");
    } else {
        ade_text(channel, 430, 318, COLOR_BLUE, "高级选项");
        ade_cursor(channel, 430, 306, 166, 34, "hand");
    }

    if (g_target_count > 0) {
        draw_button(channel, 24, 350, 260, 52, COLOR_BLUE, "安装 Zhaoxu OS");
        ade_cursor(channel, 24, 350, 260, 52, "hand");
    }
    ade_round(channel, 300, 350, 150, 52, 10, 0xDCE6EF);
    ade_text(channel, 332, 366, COLOR_DARK, "刷新磁盘");
    ade_cursor(channel, 300, 350, 150, 52, "hand");
    ade_commit(channel);
}

static void draw_confirm(long channel)
{
    ade_clear(channel, COLOR_PANEL);
    ade_fill(channel, 0, 0, WINDOW_W, 72, COLOR_BLUE);
    ade_text(channel, 24, 16, 0xFFFFFF, "确认安装");

    ade_text(channel, 24, 100, COLOR_DARK, "即将把 Zhaoxu OS 安装到：");
    char label[128];
    target_label(label, sizeof(label), g_selected);
    ade_text(channel, 32, 132, COLOR_BLUE, label);
    ade_text(channel, 24, 178, COLOR_RED,
             "该磁盘上的所有数据将被清空，此操作不可撤销。");
    ade_text(channel, 24, 206, COLOR_GRAY,
             g_gpt ? "分区方案：GPT" : "分区方案：MBR");
    ade_text(channel, 24, 234, COLOR_GRAY,
             "安装过程中请勿关闭电源或拔出安装介质。");

    ade_round(channel, 24, 300, 180, 52, 10, 0xDCE6EF);
    ade_text(channel, 82, 316, COLOR_DARK, "返回");
    ade_cursor(channel, 24, 300, 180, 52, "hand");
    draw_button(channel, 232, 300, 260, 52, COLOR_RED, "开始安装");
    ade_cursor(channel, 232, 300, 260, 52, "hand");
    ade_commit(channel);
}

static void draw_installing(long channel, unsigned int done,
                            unsigned int total)
{
    ade_clear(channel, COLOR_PANEL);
    ade_fill(channel, 0, 0, WINDOW_W, 72, COLOR_BLUE);
    ade_text(channel, 24, 16, 0xFFFFFF, "正在安装 Zhaoxu OS");
    ade_text(channel, 24, 116, COLOR_DARK,
             "正在分区、格式化并复制系统文件…");
    ade_text(channel, 24, 150, COLOR_GRAY,
             "请勿关闭电源或拔出安装介质。");

    if (total == 0)
        total = 1;
    if (done > total)
        done = total;
    int width = (int)(572u * done / total);
    if (width < 9)
        width = 9;
    ade_round(channel, 24, 210, 572, 18, 9, 0xDCE6EF);
    ade_round(channel, 24, 210, width, 18, 9, COLOR_BLUE);

    char label[32];
    int at = 0;
    label[0] = 0;
    append_text(label, sizeof(label), &at, "写入阶段 ");
    append_uint(label, sizeof(label), &at, done);
    append_text(label, sizeof(label), &at, " / ");
    append_uint(label, sizeof(label), &at, total);
    ade_text(channel, 24, 242, COLOR_GRAY, label);
    ade_cursor(channel, 0, 0, WINDOW_W, WINDOW_H, "wait");
    ade_commit(channel);
}

static void draw_done(long channel)
{
    ade_clear(channel, COLOR_PANEL);
    ade_fill(channel, 0, 0, WINDOW_W, 72, COLOR_GREEN);
    ade_text(channel, 24, 16, 0xFFFFFF, "安装完成");
    ade_text(channel, 24, 110, COLOR_DARK,
             "Zhaoxu OS 已安装到目标磁盘。");
    ade_text(channel, 24, 144, COLOR_GRAY,
             "请取出 ISO，然后从目标磁盘启动。");
    draw_button(channel, 24, 210, 200, 52, COLOR_BLUE, "立即重启");
    ade_cursor(channel, 24, 210, 200, 52, "hand");
    ade_round(channel, 244, 210, 200, 52, 10, 0xDCE6EF);
    ade_text(channel, 266, 226, COLOR_DARK, "关闭安装程序");
    ade_cursor(channel, 244, 210, 200, 52, "hand");
    ade_commit(channel);
}

static void draw_failed(long channel)
{
    ade_clear(channel, COLOR_PANEL);
    ade_fill(channel, 0, 0, WINDOW_W, 72, COLOR_RED);
    ade_text(channel, 24, 16, 0xFFFFFF, "安装失败");
    ade_text(channel, 24, 110, COLOR_DARK, "系统没有写入目标磁盘。");
    ade_text(channel, 24, 144, COLOR_GRAY,
             g_error[0] ? g_error : "未知错误，请检查磁盘后重试。");
    draw_button(channel, 24, 210, 200, 52, COLOR_BLUE, "返回");
    ade_cursor(channel, 24, 210, 200, 52, "hand");
    ade_commit(channel);
}

/* Compare a command prefix without pulling in libc. */
static int starts_with(const char *text, const char *prefix)
{
    while (*prefix) {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

/* Worker mode: run the blocking install and publish progress/result into the
 * shared page the UI process mapped before spawning this child. */
static int worker_main(const char *command)
{
    const char *cursor = command + 6;
    unsigned long id = parse_uint(cursor, &cursor);
    unsigned long disk = parse_uint(cursor, &cursor);
    unsigned long gpt = parse_uint(cursor, &cursor);
    struct zos_install_progress *progress =
        (struct zos_install_progress *)sys_shm_map((long)id);
    if (!progress)
        return 1;

    progress->done = 0;
    progress->total = 9;
    progress->finished = 0;
    progress->result = -1;
    progress->error[0] = 0;

    long result = sys_install_progress((long)disk, (long)gpt, progress);
    if (result != 0) {
        sys_install_error(progress->error, sizeof(progress->error) - 1);
        if (!progress->error[0]) {
            const char *fallback =
                result == -3 ? "未找到可安装的空白磁盘。"
                             : "写入磁盘失败，请检查磁盘后重试。";
            copy_text(progress->error, sizeof(progress->error), fallback,
                      text_length(fallback));
        }
    }
    progress->result = (int)result;
    __asm__ volatile("" ::: "memory");
    progress->finished = 1;
    return result == 0 ? 0 : 1;
}

/* Create the shared progress page and spawn the worker child. */
static int start_worker(int disk, int gpt)
{
    g_progress_id = (long)sys_shm_create(sizeof(struct zos_install_progress));
    if (g_progress_id <= 0)
        return -1;
    g_progress = (struct zos_install_progress *)sys_shm_map(g_progress_id);
    if (!g_progress)
        return -1;
    g_progress->done = 0;
    g_progress->total = 9;
    g_progress->finished = 0;
    g_progress->result = -1;
    g_progress->error[0] = 0;

    char argument[64];
    int at = 0;
    argument[0] = 0;
    append_text(argument, sizeof(argument), &at, "worker ");
    append_uint(argument, sizeof(argument), &at, (unsigned long)g_progress_id);
    append_text(argument, sizeof(argument), &at, " ");
    append_uint(argument, sizeof(argument), &at, (unsigned long)disk);
    append_text(argument, sizeof(argument), &at, " ");
    append_uint(argument, sizeof(argument), &at, (unsigned long)(gpt ? 1 : 0));

    g_worker_pid = sys_spawn_arg("/bin/apps/install.zxe", argument);
    return g_worker_pid > 0 ? 0 : -1;
}

static void start_install(long channel)
{
    g_state = STATE_INSTALLING;
    u_puts("[install] installing\n");
    if (start_worker(g_targets[g_selected].index, g_gpt) != 0) {
        g_state = STATE_FAILED;
        copy_text(g_error, sizeof(g_error), "无法启动安装进程。", 24);
        draw_failed(channel);
        return;
    }

    unsigned int last_done = 0xFFFFFFFFu;
    unsigned int last_total = 0xFFFFFFFFu;
    draw_installing(channel, 0, 9);
    for (;;) {
        unsigned int done = g_progress->done;
        unsigned int total = g_progress->total;
        if (done != last_done || total != last_total) {
            last_done = done;
            last_total = total;
            draw_installing(channel, done, total);
        }
        struct ade_message message;
        while (ade_poll(channel, &message)) {
            /* Ignore close/click while the worker owns the disk. */
        }
        if (g_progress->finished)
            break;
        sys_sleep(20);
    }
    __asm__ volatile("" ::: "memory");

    int status = 0;
    if (g_worker_pid > 0)
        sys_wait(g_worker_pid, &status);
    if (g_progress->result == 0) {
        g_state = STATE_DONE;
        u_puts("[install] complete\n");
        draw_done(channel);
        return;
    }

    g_state = STATE_FAILED;
    int at = 0;
    for (; at + 1 < (int)sizeof(g_error) && g_progress->error[at]; at++)
        g_error[at] = g_progress->error[at];
    g_error[at] = 0;
    if (!g_error[0])
        copy_text(g_error, sizeof(g_error), "写入磁盘失败，请检查磁盘后重试。",
                  48);
    u_puts("[install] failed\n");
    draw_failed(channel);
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    const char *command = (argc > 0 && argv[0]) ? argv[0] : "";
    if (starts_with(command, "worker "))
        return worker_main(command);

    int force = command[0] == 'f';
    if (!force && sys_data_volume())
        return 0;

    if (refresh_disks() <= 0)
        return 0;

    long channel = ade_connect();
    if (channel <= 0)
        return 1;
    if (ade_create_window(channel, WINDOW_W, WINDOW_H, "安装", 'I',
                          COLOR_BLUE) != 0)
        return 2;
    u_puts("[install] window ready\n");

    g_state = STATE_SELECT;
    draw_select(channel);

    for (;;) {
        struct ade_message message;
        if (!ade_poll(channel, &message)) {
            sys_sleep(10);
            continue;
        }
        if (message.type == ADE_EV_CLOSE)
            break;
        if (message.type != ADE_EV_CLICK)
            continue;

        if (g_state == STATE_SELECT) {
            if (message.y >= 306 && message.y < 340 &&
                message.x >= 430 && message.x < 596) {
                if (g_advanced)
                    g_gpt = !g_gpt;
                else
                    g_advanced = 1;
                draw_select(channel);
                continue;
            }
            int visible = g_target_count < VISIBLE_TARGETS ? g_target_count
                                                           : VISIBLE_TARGETS;
            for (int i = 0; i < visible; i++) {
                int y = target_row_y(i);
                if (message.x >= 24 && message.x < 596 &&
                    message.y >= y && message.y < y + 38) {
                    g_selected = i;
                    draw_select(channel);
                    break;
                }
            }
            if (message.x >= 300 && message.x < 450 && message.y >= 350 &&
                message.y < 402) {
                refresh_disks();
                draw_select(channel);
                continue;
            }
            if (g_target_count > 0 && message.x >= 24 &&
                message.x < 284 && message.y >= 350 && message.y < 402) {
                g_state = STATE_CONFIRM;
                draw_confirm(channel);
            }
        } else if (g_state == STATE_CONFIRM) {
            if (message.x >= 24 && message.x < 204 && message.y >= 300 &&
                message.y < 352) {
                g_state = STATE_SELECT;
                draw_select(channel);
            } else if (message.x >= 232 && message.x < 492 &&
                       message.y >= 300 && message.y < 352) {
                start_install(channel);
            }
        } else if (g_state == STATE_DONE) {
            if (message.x >= 24 && message.x < 224 && message.y >= 210 &&
                message.y < 262) {
                u_puts("[install] rebooting\n");
                sys_reboot();
            }
            if (message.x >= 244 && message.x < 444 && message.y >= 210 &&
                message.y < 262)
                break;
        } else if (g_state == STATE_FAILED) {
            if (message.x >= 24 && message.x < 224 && message.y >= 210 &&
                message.y < 262) {
                g_state = STATE_SELECT;
                refresh_disks();
                draw_select(channel);
            }
        }
        sys_sleep(10);
    }

    sys_port_close(channel);
    return 0;
}
