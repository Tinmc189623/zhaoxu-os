/*
 * Zhaoxu OS - setup program, in the Windows XP i386 tradition.
 *
 * Text-mode setup on the kernel console: welcome, licence, disk selection,
 * layout confirmation, format warning, progress and restart.  The medium runs
 * setup and nothing else; there is no desktop, no compositor and no live
 * session to explore.
 */
#include <userlib.h>

#define MAX_TARGETS 8

struct target_disk {
    int index;
    unsigned long mib;
    char model[56];
};

static struct target_disk g_targets[MAX_TARGETS];
static char g_disks[1024];
static int g_target_count;
static int g_selected;
static int g_gpt = 1;
static int g_data;
static long g_progress_id;
static struct zos_install_progress *g_progress;
static char g_error[128];

/* ---- small text helpers ------------------------------------------------ */

static void copy_text(char *destination, int capacity, const char *source,
                      int length)
{
    if (length >= capacity)
        length = capacity - 1;
    for (int i = 0; i < length; i++)
        destination[i] = source[i];
    destination[length] = 0;
}

static int starts_with(const char *text, const char *prefix)
{
    while (*prefix) {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static unsigned long parse_uint(const char *text, const char **end_out)
{
    unsigned long value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (unsigned long)(*text - '0');
        text++;
    }
    if (end_out)
        *end_out = text;
    return value;
}

static void put_uint(unsigned long value)
{
    char digits[24];
    int at = 0;
    do {
        digits[at++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && at < (int)sizeof(digits));
    for (int i = 0; i < at / 2; i++) {
        char swap = digits[i];
        digits[i] = digits[at - 1 - i];
        digits[at - 1 - i] = swap;
    }
    digits[at] = 0;
    u_puts(digits);
}

/* Append a decimal number to a command line under construction. */
static void append_uint(char *buffer, int *at, unsigned long value)
{
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < (int)sizeof(digits));
    while (count--)
        buffer[(*at)++] = digits[count];
}

static void line(const char *text)
{
    u_puts(text);
    u_puts("\n");
}

/* Push earlier screens off the 80x25 console. */
static void clear_screen(void)
{
    for (int i = 0; i < 30; i++)
        u_puts("\n");
}

static void enter_raw_mode(void)
{
    struct termios termios;
    if (sys_ioctl(0, TCGETS, &termios) != 0)
        return;
    termios.c_lflag &= ~(ICANON | ECHO | ISIG);
    sys_ioctl(0, TCSETS, &termios);
}

/* Raw mode delivers one key per read; letters arrive in either case. */
static int read_key(void)
{
    char key = 0;
    for (;;) {
        long got = sys_read(0, &key, 1);
        if (got == 1)
            return (unsigned char)key;
        sys_sleep(5);
    }
}

static int key_is(int key, char letter, char upper)
{
    return key == letter || key == upper;
}

/* ---- disk table -------------------------------------------------------- */

static void parse_targets(const char *text)
{
    g_target_count = 0;
    const char *cursor = text;
    while (*cursor && g_target_count < MAX_TARGETS) {
        const char *end = cursor;
        while (*end && *end != '\n')
            end++;

        const char *field[8];
        int count = 0;
        const char *p = cursor;
        while (p < end && count < 8) {
            while (p < end && *p == ' ')
                p++;
            if (p >= end)
                break;
            field[count++] = p;
            while (p < end && *p != ' ')
                p++;
        }

        if (count >= 6 && starts_with(field[5], "target") &&
            (field[5] + 6 == end || field[5][6] == ' ')) {
            struct target_disk *target = &g_targets[g_target_count++];
            target->index = (int)parse_uint(field[0], 0);
            target->mib = parse_uint(field[2], 0);
            target->model[0] = 0;
            if (count >= 7) {
                const char *model = field[6];
                int length = (int)(end - model);
                while (length > 0 &&
                       (model[length - 1] == '\r' || model[length - 1] == ' '))
                    length--;
                copy_text(target->model, sizeof(target->model), model, length);
            }
            if (!target->model[0])
                copy_text(target->model, sizeof(target->model), "disk", 4);
        }
        cursor = *end ? end + 1 : end;
    }
    if (g_selected >= g_target_count)
        g_selected = 0;
}

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

/* ---- setup worker ------------------------------------------------------ */

static int worker_main(const char *command)
{
    const char *cursor = command + 6;
    unsigned long id = parse_uint(cursor, &cursor);
    unsigned long disk = parse_uint(cursor, &cursor);
    unsigned long gpt = parse_uint(cursor, &cursor);
    unsigned long esp_mib = parse_uint(cursor, &cursor);
    unsigned long data_mib = parse_uint(cursor, 0);
    struct zos_install_progress *progress =
        (struct zos_install_progress *)sys_shm_map((long)id);
    if (!progress)
        return 1;

    progress->done = 0;
    progress->total = 10;
    progress->finished = 0;
    progress->result = -1;
    progress->error[0] = 0;

    struct zos_install_config config;
    config.target = disk;
    config.gpt = gpt;
    config.progress = progress;
    config.progress_capacity = sizeof(*progress);
    config.esp_mib = esp_mib;
    config.data_mib = data_mib;
    long result = sys_install_config(&config);
    if (result != 0) {
        sys_install_error(progress->error, sizeof(progress->error) - 1);
        if (!progress->error[0])
            copy_text(progress->error, sizeof(progress->error),
                      "disk write failed", 16);
    }
    progress->result = (int)result;
    __asm__ volatile("" ::: "memory");
    progress->finished = 1;
    return result == 0 ? 0 : 1;
}

static int start_worker(int disk, int gpt)
{
    char argument[96];
    int at = 0;
    const char *prefix = "worker ";
    while (*prefix)
        argument[at++] = *prefix++;
    append_uint(argument, &at, (unsigned long)g_progress_id);
    argument[at++] = ' ';
    append_uint(argument, &at, (unsigned long)disk);
    argument[at++] = ' ';
    append_uint(argument, &at, (unsigned long)gpt);
    argument[at++] = ' ';
    append_uint(argument, &at, 64UL);            /* ESP size in MiB */
    argument[at++] = ' ';
    append_uint(argument, &at, g_data ? 128UL : 0UL);
    argument[at] = 0;
    return (int)sys_spawn_arg("/bin/apps/install.zxe", argument);
}

/* ---- screens ----------------------------------------------------------- */

static void banner(void)
{
    clear_screen();
    line("Setup");
    line("=====");
    line("");
}

static int screen_welcome(void)
{
    banner();
    line("Welcome to Setup.");
    line("");
    line("This portion of the Setup program prepares your computer");
    line("to run the operating system.");
    line("");
    line("  ENTER  Begin setup");
    line("  F3     Quit setup");
    line("");
    for (;;) {
        int key = read_key();
        if (key == '\r' || key == '\n')
            return 1;
        if (key == 0xF3 || key_is(key, 'q', 'Q')) {
            line("");
            line("Setup has been cancelled. Remove the disc and restart.");
            return 0;
        }
    }
}

static int screen_license(void)
{
    banner();
    line("End-User Licence Agreement");
    line("");
    line("This system is distributed under the GNU General Public");
    line("License version 2.  Third-party components keep their own");
    line("licences.  Installing the system means accepting these terms.");
    line("");
    line("  F8     I agree");
    line("  ESC    I do not agree and quit");
    line("");
    for (;;) {
        int key = read_key();
        if (key == 0xF8 || key_is(key, 'y', 'Y'))
            return 1;
        if (key == 0x1b || key_is(key, 'n', 'N')) {
            line("");
            line("Setup has been cancelled. Remove the disc and restart.");
            return 0;
        }
    }
}

static int screen_disks(void)
{
    for (;;) {
        int count = refresh_disks();
        banner();
        line("The following disks can hold the installation.");
        line("");
        if (count <= 0) {
            line("  No installation disk was found.");
            line("");
            line("  R      Rescan");
            line("  F3     Quit setup");
        } else {
            for (int i = 0; i < count; i++) {
                u_puts(i == g_selected ? "  > " : "    ");
                put_uint((unsigned long)(i + 1));
                u_puts(". ");
                u_puts(g_targets[i].model);
                u_puts("  ");
                put_uint(g_targets[i].mib);
                line(" MiB");
            }
            line("");
            line("  Up/Down  Select a disk");
            line("  ENTER    Continue with the highlighted disk");
            line("  R        Rescan");
            line("  F3       Quit setup");
        }
        line("");

        int key = read_key();
        if (key == 0xF3 || key_is(key, 'q', 'Q')) {
            line("Setup has been cancelled.");
            return 0;
        }
        if (key_is(key, 'r', 'R'))
            continue;
        if (count > 0 && key == 0x81)
            g_selected = (g_selected + count - 1) % count;
        else if (count > 0 && key == 0x82)
            g_selected = (g_selected + 1) % count;
        else if (key >= '1' && key < '1' + count)
            g_selected = key - '1';
        else if ((key == '\r' || key == '\n') && count > 0) {
            line("");
            u_puts("Selected: ");
            u_puts(g_targets[g_selected].model);
            u_puts("  ");
            put_uint(g_targets[g_selected].mib);
            line(" MiB");
            return 1;
        }
    }
}

static int screen_layout(void)
{
    for (;;) {
        banner();
        line("Installation layout");
        line("");
        u_puts("  Disk       ");
        u_puts(g_targets[g_selected].model);
        u_puts("  ");
        put_uint(g_targets[g_selected].mib);
        line(" MiB");
        u_puts("  Partition  ");
        line(g_gpt ? "GPT" : "MBR");
        u_puts("  Data disk  ");
        line(g_data ? "yes (optional ZHAOXUDATA)" : "no");
        line("");
        line("  ENTER  Install on this layout");
        line("  T      Switch between GPT and MBR");
        line("  D      Toggle the optional data partition");
        line("  ESC    Back to the disk list");
        line("");

        int key = read_key();
        if (key == '\r' || key == '\n')
            return 1;
        if (key_is(key, 't', 'T'))
            g_gpt = !g_gpt;
        else if (key_is(key, 'd', 'D'))
            g_data = !g_data;
        else if (key == 0x1b)
            return 0;
    }
}

static int screen_confirm(void)
{
    banner();
    line("Format warning");
    line("");
    line("Everything on this disk is erased:");
    line("");
    u_puts("  ");
    u_puts(g_targets[g_selected].model);
    u_puts("  ");
    put_uint(g_targets[g_selected].mib);
    line(" MiB");
    line("");
    line("  F      Format the disk and install");
    line("  ESC    Back");
    line("");
    for (;;) {
        int key = read_key();
        if (key_is(key, 'f', 'F'))
            return 1;
        if (key == 0x1b)
            return 0;
    }
}

static void show_progress(unsigned long done, unsigned long total)
{
    line("");
    u_puts("  Installing: ");
    put_uint(done);
    u_puts(" of ");
    put_uint(total);
    u_puts(" (");
    put_uint(total ? done * 100 / total : 0);
    line("%)");
}

static int run_install(void)
{
    g_progress_id = sys_shm_create(sizeof(struct zos_install_progress));
    if (g_progress_id <= 0)
        return -1;
    g_progress = (struct zos_install_progress *)sys_shm_map(g_progress_id);
    if (!g_progress)
        return -1;
    g_progress->done = 0;
    g_progress->total = 10;
    g_progress->finished = 0;
    g_progress->result = -1;
    g_progress->error[0] = 0;

    banner();
    line("Please wait while Setup installs the system.");
    line("");
    line("  This takes several minutes.  Do not remove the disc.");
    show_progress(0, 10);

    int worker = start_worker(g_targets[g_selected].index, g_gpt);
    if (worker <= 0)
        return -1;

    unsigned long last_done = 0xFFFFFFFFu;
    unsigned long last_total = 0xFFFFFFFFu;
    for (;;) {
        unsigned long done = g_progress->done;
        unsigned long total = g_progress->total;
        if (done != last_done || total != last_total) {
            show_progress(done, total);
            last_done = done;
            last_total = total;
        }
        if (g_progress->finished)
            break;
        sys_sleep(100);
    }
    __asm__ volatile("" ::: "memory");

    int status = 0;
    sys_wait(worker, &status);
    if (g_progress->result == 0) {
        u_puts("[install] complete\n");
        return 0;
    }

    int at = 0;
    for (; at + 1 < (int)sizeof(g_error) && g_progress->error[at]; at++)
        g_error[at] = g_progress->error[at];
    g_error[at] = 0;
    if (!g_error[0])
        copy_text(g_error, sizeof(g_error), "disk write failed", 16);
    u_puts("[install] failed\n");
    return -1;
}

static void screen_done(void)
{
    banner();
    line("Setup complete.");
    line("");
    line("Setup is complete.  Remove the disc before the");
    line("machine restarts.");
    line("");
    line("  ENTER  Restart now");
    line("  ESC    Stay here");
    line("");
    for (;;) {
        int key = read_key();
        if (key == '\r' || key == '\n') {
            u_puts("[install] rebooting\n");
            sys_reboot();
        }
        if (key == 0x1b)
            return;
    }
}

static void screen_failed(void)
{
    banner();
    line("Setup could not finish.");
    line("");
    line(g_error);
    line("");
    line("  ENTER  Return to the setup screens");
    line("");
    for (;;) {
        int key = read_key();
        if (key == '\r' || key == '\n')
            return;
    }
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    const char *command = (argc > 0 && argv[0]) ? argv[0] : "";
    if (starts_with(command, "worker "))
        return worker_main(command);

    /* XP setup colours: light grey text on blue. */
    sys_console_color(USER_COLOR_LIGHT_GRAY, USER_COLOR_BLUE);
    enter_raw_mode();
    if (!screen_welcome())
        return 0;
    if (!screen_license())
        return 0;
    for (;;) {
        if (!screen_disks())
            return 0;
        if (!screen_layout())
            continue;
        if (!screen_confirm())
            continue;
        if (run_install() == 0) {
            screen_done();
            return 0;
        }
        screen_failed();
    }
}
