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
static int g_target_count;
static int g_selected;
static int g_gpt = 1;
static int g_advanced;
static enum install_state g_state;
static char g_error[160];

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

static void draw_installing(long channel)
{
    ade_clear(channel, COLOR_PANEL);
    ade_fill(channel, 0, 0, WINDOW_W, 72, COLOR_BLUE);
    ade_text(channel, 24, 16, 0xFFFFFF, "正在安装 Zhaoxu OS");
    ade_text(channel, 24, 116, COLOR_DARK,
             "正在分区、格式化并复制系统文件…");
    ade_text(channel, 24, 150, COLOR_GRAY,
             "请勿关闭电源或拔出安装介质。");
    ade_round(channel, 24, 210, 572, 18, 9, 0xDCE6EF);
    ade_round(channel, 24, 210, 360, 18, 9, COLOR_BLUE);
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
    draw_button(channel, 24, 210, 200, 52, COLOR_BLUE, "关闭安装程序");
    ade_cursor(channel, 24, 210, 200, 52, "hand");
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

static void start_install(long channel)
{
    g_state = STATE_INSTALLING;
    draw_installing(channel);
    u_puts("[install] installing\n");
    long result = sys_install(g_targets[g_selected].index, g_gpt);
    if (result == 0) {
        g_state = STATE_DONE;
        u_puts("[install] complete\n");
        draw_done(channel);
        return;
    }
    g_state = STATE_FAILED;
    g_error[0] = 0;
    sys_install_error(g_error, sizeof(g_error) - 1);
    if (!g_error[0]) {
        const char *fallback = result == -3 ? "未找到可安装的空白磁盘。"
                                            : "写入磁盘失败，请检查磁盘后重试。";
        copy_text(g_error, sizeof(g_error), fallback, text_length(fallback));
    }
    u_puts("[install] failed\n");
    draw_failed(channel);
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    int force = 0;
    if (argc > 0 && argv[0] && argv[0][0] == 'f')
        force = 1;

    if (!force && sys_data_volume())
        return 0;

    static char disks[1024];
    long count = sys_disks(disks, sizeof(disks) - 1);
    if (count <= 0)
        return 0;
    if (count >= (long)sizeof(disks))
        count = (long)sizeof(disks) - 1;
    disks[count] = 0;
    parse_targets(disks);

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
                message.y < 262)
                break;
        } else if (g_state == STATE_FAILED) {
            if (message.x >= 24 && message.x < 224 && message.y >= 210 &&
                message.y < 262) {
                g_state = STATE_SELECT;
                parse_targets(disks);
                draw_select(channel);
            }
        }
        sys_sleep(10);
    }

    sys_port_close(channel);
    return 0;
}
