/*
 * Zhaoxu OS - Yuanshuo shell
 *
 * The user-facing layer is written in C++20 and linked directly into the
 * kernel.  It only uses the freestanding kernel services (console, memory,
 * timers, ACPI, PCI and the Ada drivers).
 */
extern "C" {
#include <acpi.h>
#include <boot.h>
#include <console.h>
#include <cpu.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <pci.h>
#include <process.h>
#include <rtc.h>
#include <shell.h>
#include <string.h>
#include <timer.h>
#include <ttf.h>
#include <types.h>
#include <version.h>
#include <vfs.h>
}

int desktop_run(int argc, char **argv);

namespace {

constexpr const char *kVersion = ZHAOXU_VERSION;
constexpr const char *kCodename = ZHAOXU_CODENAME;
constexpr usize kMaxLine = 256;
constexpr int kMaxArgs = 8;

char g_line[kMaxLine];
usize g_line_length;

struct command {
    const char *name;
    void (*handler)(int argc, char **argv);
    const char *description;
};

void cmd_help(int argc, char **argv);
void cmd_clear(int argc, char **argv);
void cmd_echo(int argc, char **argv);
void cmd_version(int argc, char **argv);
void cmd_uname(int argc, char **argv);
void cmd_mem(int argc, char **argv);
void cmd_uptime(int argc, char **argv);
void cmd_cpu(int argc, char **argv);
void cmd_date(int argc, char **argv);
void cmd_pci(int argc, char **argv);
void cmd_acpi(int argc, char **argv);
void cmd_ls(int argc, char **argv);
void cmd_cat(int argc, char **argv);
void cmd_run(int argc, char **argv);
void cmd_fonttest(int argc, char **argv);
void cmd_mounts(int argc, char **argv);
void cmd_ps(int argc, char **argv);
void cmd_bench(int argc, char **argv);
void cmd_sleep(int argc, char **argv);
void cmd_desktop(int argc, char **argv);
void cmd_color(int argc, char **argv);
void cmd_neofetch(int argc, char **argv);
void cmd_reboot(int argc, char **argv);
void cmd_shutdown(int argc, char **argv);
void cmd_panic(int argc, char **argv);

const command kCommands[] = {
    { "help",     cmd_help,     "show this command list" },
    { "clear",    cmd_clear,    "clear the screen" },
    { "echo",     cmd_echo,     "print the given text" },
    { "version",  cmd_version,  "version and build information" },
    { "uname",    cmd_uname,    "system identification" },
    { "mem",      cmd_mem,      "physical and heap memory statistics" },
    { "uptime",   cmd_uptime,   "time since boot" },
    { "cpu",      cmd_cpu,      "processor details and features" },
    { "date",     cmd_date,     "read the CMOS real-time clock" },
    { "pci",      cmd_pci,      "enumerate PCI devices" },
    { "acpi",     cmd_acpi,     "ACPI power management information" },
    { "ls",       cmd_ls,       "list a directory (default /)" },
    { "cat",      cmd_cat,      "print a file from ramfs or FAT32" },
    { "run",      cmd_run,      "execute an .zxe or ELF64 program in ring 3" },
    { "fonttest", cmd_fonttest, "render a Unicode glyph as ASCII art" },
    { "mounts",   cmd_mounts,   "show mounted filesystems" },
    { "ps",       cmd_ps,       "show ring 3 process state" },
    { "bench",    cmd_bench,    "memory and TSC micro-benchmarks" },
    { "sleep",    cmd_sleep,    "sleep for the given milliseconds" },
    { "desktop",  cmd_desktop,  "start the graphical Desktop-Shell (ESC exits)" },
    { "color",    cmd_color,    "set console colours (0-15)" },
    { "neofetch", cmd_neofetch, "show a system summary" },
    { "reboot",   cmd_reboot,   "restart the machine through ACPI" },
    { "shutdown", cmd_shutdown, "power off the machine through ACPI" },
    { "panic",    cmd_panic,    "trigger a kernel page fault (test)" },
};

constexpr usize kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

void copy_trunc(char *destination, usize size, const char *source)
{
    if (size == 0)
        return;
    usize index = 0;
    while (source[index] && index + 1 < size) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = 0;
}

void format_uptime(char *buffer, usize size)
{
    u64 ticks = timer_ticks();
    u32 hz = timer_hz();
    u64 seconds = ticks / hz;
    u64 hundredths = ((ticks % hz) * 100) / hz;
    ksnprintf(buffer, size, "%llu.%02llu s", seconds, hundredths);
}

const char *pixel_format_name(u32 format)
{
    switch (format) {
    case 0: return "RGBX";
    case 1: return "BGRX";
    case 2: return "BitMask";
    case 3: return "BltOnly";
    default: return "Unknown";
    }
}

void print_prompt()
{
    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("zhaoxu> ");
    console_reset_color();
}

void print_error(const char *message)
{
    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("%s\n", message);
    console_reset_color();
}

void print_usage(const char *usage)
{
    console_set_color(COLOR_YELLOW, COLOR_BLACK);
    kprintf("usage: %s\n", usage);
    console_reset_color();
}

void cmd_help(int, char **)
{
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Yuanshuo shell - built-in commands\n");
    console_reset_color();
    for (usize i = 0; i < kCommandCount; i++)
        kprintf("  %-10s %s\n", kCommands[i].name, kCommands[i].description);
    kprintf("\nLine editing: Backspace, Ctrl+L clear screen, Ctrl+C cancel.\n");
}

void cmd_clear(int, char **)
{
    console_clear();
}

void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            console_putc(' ');
        console_write(argv[i]);
    }
    console_putc('\n');
}

void cmd_version(int, char **)
{
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Zhaoxu OS %s \"%s\"\n", kVersion, kCodename);
    console_reset_color();
    kprintf("Built %s %s\n", __DATE__, __TIME__);
    kprintf("Toolchain: FASM, GCC, GNAT (Ada), ld.lld\n");
    kprintf("Licence: Apache License 2.0\n");
}

void cmd_uname(int, char **)
{
    kprintf("Zhaoxu %s %s x86_64 UEFI higher-half\n",
            kCodename, kVersion);
    kprintf("kernel: Yuanshuo (multilingual microkernel-style monitor)\n");
}

void cmd_mem(int, char **)
{
    struct pmm_stats physical;
    pmm_get_stats(&physical);

    u64 heap_total = 0, heap_used = 0, heap_free = 0;
    heap_get_stats(&heap_total, &heap_used, &heap_free);

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Physical memory\n");
    console_reset_color();
    kprintf("  managed : %llu MiB (%llu pages)\n",
            (physical.managed_pages * PAGE_SIZE) >> 20,
            physical.managed_pages);
    kprintf("  free    : %llu MiB (%llu pages)\n",
            (physical.free_pages * PAGE_SIZE) >> 20,
            physical.free_pages);
    kprintf("  used    : %llu MiB (%llu pages)\n",
            (physical.used_pages * PAGE_SIZE) >> 20,
            physical.used_pages);

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Kernel heap\n");
    console_reset_color();
    kprintf("  total   : %llu KiB\n", heap_total >> 10);
    kprintf("  used    : %llu KiB\n", heap_used >> 10);
    kprintf("  free    : %llu KiB\n", heap_free >> 10);
}

void cmd_uptime(int, char **)
{
    char formatted[64];
    format_uptime(formatted, sizeof(formatted));
    kprintf("up %s (%llu ticks at %u Hz)\n",
            formatted, timer_ticks(), timer_hz());
}

void cmd_cpu(int, char **)
{
    const struct cpu_info *cpu = cpu_get_info();
    char features[256];
    cpu_features_string(features, sizeof(features));

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Processor\n");
    console_reset_color();
    kprintf("  vendor   : %s\n", cpu->vendor);
    kprintf("  brand    : %s\n", cpu->brand);
    kprintf("  family   : %u  model: %u  stepping: %u\n",
            cpu->family, cpu->model, cpu->stepping);
    kprintf("  logical  : %u\n", cpu->logical_cpus ? cpu->logical_cpus : 1);
    kprintf("  features : %s\n", features);
    kprintf("  tsc      : %llu cycles (current sample)\n", cpu_rdtsc());
}

void cmd_date(int, char **)
{
    static const char *const weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    static const char *const months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    struct rtc_time now;
    if (rtc_read(&now) != 0) {
        print_error("RTC read failed");
        return;
    }

    u32 weekday = now.weekday;
    if (weekday < 1 || weekday > 7)
        weekday = 0;
    else
        weekday--;

    u32 month = now.month;
    const char *month_name = (month >= 1 && month <= 12)
                                 ? months[month - 1] : "Unknown";

    kprintf("CMOS RTC (UTC): %04u-%02u-%02u %s %02u:%02u:%02u\n",
            now.year, now.month, now.day, weekdays[weekday],
            now.hour, now.minute, now.second);
    kprintf("              %s %u, %04u\n", month_name, now.day, now.year);
}

void cmd_pci(int, char **)
{
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("PCI configuration space scan\n");
    console_reset_color();
    pci_enumerate();
}

void cmd_acpi(int, char **)
{
    const struct acpi_info *acpi = acpi_get_info();

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("ACPI power management\n");
    console_reset_color();

    if (!acpi->present) {
        print_error("  ACPI RSDP was not found");
        return;
    }

    kprintf("  RSDP revision : %u\n", acpi->rsdp_revision);
    kprintf("  OEM ID        : %s\n", acpi->oem_id);
    kprintf("  tables        : %u\n", acpi->table_count);
    kprintf("  FADT revision : %u\n", acpi->fadt_revision);
    kprintf("  PM1a control  : 0x%x\n", acpi->pm1a_control);
    kprintf("  PM1b control  : 0x%x\n", acpi->pm1b_control);
    if (acpi->slp_typa_valid)
        kprintf("  S5 sleep type : %u (parsed from DSDT _S5)\n",
                acpi->slp_typa);
    else
        kprintf("  S5 sleep type : unknown (fallback probing)\n");
    if (acpi->reset_supported)
        kprintf("  reset         : %s 0x%llx, value 0x%x\n",
                acpi->reset_address_space == 1 ? "I/O" :
                acpi->reset_address_space == 0 ? "memory" : "space",
                acpi->reset_address, acpi->reset_value);
    else
        kprintf("  reset         : legacy fallback\n");
}

void list_entry(const char *name, u64 size, int is_dir, void *)
{
    if (is_dir) {
        console_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
        kprintf("  %-32s <DIR>\n", name);
    } else {
        console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
        kprintf("  %-32s %llu B\n", name, size);
    }
    console_reset_color();
}

void cmd_ls(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/";

    int is_dir = 0;
    u64 size = 0;
    if (vfs_stat(path, &is_dir, &size) != 0) {
        console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        kprintf("ls: %s: no such file or directory\n", path);
        console_reset_color();
        return;
    }

    if (!is_dir) {
        console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
        kprintf("  %-32s %llu B\n", path, size);
        console_reset_color();
        return;
    }

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("%s:\n", path);
    console_reset_color();

    if (vfs_list(path, list_entry, nullptr) != 0) {
        console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        kprintf("ls: cannot list %s\n", path);
        console_reset_color();
    }
}

void cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        print_usage("cat <path>");
        return;
    }

    u8 *data = nullptr;
    u64 size = 0;
    if (vfs_read_all(argv[1], &data, &size) != 0) {
        console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        kprintf("cat: %s: cannot read file\n", argv[1]);
        console_reset_color();
        return;
    }

    console_write_n((const char *)data, (usize)size);
    if (size == 0 || data[size - 1] != '\n')
        console_putc('\n');
    kfree(data);
}

void cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        print_usage("run <path> [args...]");
        return;
    }

    if (!vfs_fat_mounted() && !vfs_data_fallback() &&
        strncmp(argv[1], "/mnt", 4) == 0) {
        print_error("run: no data volume (FAT32 unmounted, no embedded copy)");
        return;
    }

    console_set_color(COLOR_YELLOW, COLOR_BLACK);
    kprintf("Executing %s in ring 3...\n", argv[1]);
    console_reset_color();

    int status = process_run(argv[1], argc - 1, &argv[1]);

    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("[shell] %s exited with status %d\n", argv[1], status);
    console_reset_color();
}

void cmd_fonttest(int argc, char **argv)
{
    if (!ttf_available()) {
        print_error("fonttest: no TrueType font loaded");
        return;
    }

    u32 codepoint = 0x4E2D;
    if (argc >= 2) {
        const char *cursor = argv[1];
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X'))
            cursor += 2;
        u32 value = 0;
        int digits = 0;
        for (; *cursor; cursor++) {
            u32 digit;
            if (*cursor >= '0' && *cursor <= '9')
                digit = (u32)(*cursor - '0');
            else if (*cursor >= 'a' && *cursor <= 'f')
                digit = (u32)(*cursor - 'a' + 10);
            else if (*cursor >= 'A' && *cursor <= 'F')
                digit = (u32)(*cursor - 'A' + 10);
            else
                break;
            value = value * 16u + digit;
            digits++;
        }
        if (digits == 0 || value == 0 || value > 0x10FFFFu) {
            print_error("fonttest: expected a hex codepoint like 0x4e2d");
            return;
        }
        codepoint = value;
    }

    struct ttf_glyph glyph;
    int status = ttf_render(codepoint, &glyph);
    if (status != 0) {
        kprintf("fonttest: U+%04X render failed (%d)\n", codepoint, status);
        return;
    }
    kprintf("fonttest U+%04X: bitmap %ux%u at (%d,%d), advance %d cells\n",
            codepoint, glyph.w, glyph.h, (int)glyph.bx, (int)glyph.by,
            (int)glyph.adv);

    for (u32 row = 0; row < 16; row++) {
        char line[18];
        for (u32 col = 0; col < 16; col++) {
            u32 alpha = 0;
            i32 gx = (i32)col - (i32)glyph.bx;
            i32 gy = (i32)row - (i32)glyph.by;
            if (gx >= 0 && gy >= 0 && gx < (i32)glyph.w &&
                gy < (i32)glyph.h)
                alpha = glyph.alpha[gy * glyph.w + gx];
            line[col] = alpha >= 200 ? '#'
                        : alpha >= 70 ? '+'
                        : alpha >= 8  ? '.'
                                      : ' ';
        }
        line[16] = 0;
        kprintf("|%s|\n", line);
    }
}

void cmd_mounts(int, char **)
{
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Mounted filesystems\n");
    console_reset_color();
    kprintf("  /    ramfs (initramfs ustar, read-only)\n");
    if (vfs_fat_mounted())
        kprintf("  /mnt FAT32 \"%s\" (AHCI, read-only)\n",
                vfs_fat_label());
    else if (vfs_data_fallback())
        kprintf("  /mnt embedded data mirror /data (initramfs, read-only)\n");
    else
        kprintf("  /mnt not mounted\n");
}

void cmd_ps(int, char **)
{
    const struct process_info *info = process_get_info();

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Processes\n");
    console_reset_color();
    kprintf("  PID  STATE    ENTRY              KERNEL STACK\n");

    if (info && info->active) {
        console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        kprintf("  %-4llu ", info->pid);
        console_reset_color();
        kprintf("running  0x%016llx 0x%016llx\n",
                info->entry, info->kernel_stack);
        kprintf("  user pages: %llu KiB\n", info->user_pages / 1024);
    } else {
        console_set_color(COLOR_DARK_GRAY, COLOR_BLACK);
        kprintf("  (no ring 3 process; only the kernel shell is running)\n");
        console_reset_color();
    }
}

void cmd_bench(int, char **)
{
    constexpr usize block_size = 256 * 1024;
    constexpr u32 iterations = 200;

    u8 *buffer = (u8 *)kmalloc(block_size);
    if (!buffer) {
        print_error("bench: out of memory");
        return;
    }

    memset(buffer, 0xA5, block_size);
    u64 start = timer_ticks();
    for (u32 i = 0; i < iterations; i++)
        memset(buffer, (int)(i & 0xFF), block_size);
    u64 elapsed = timer_ticks() - start;
    if (elapsed == 0)
        elapsed = 1;

    u64 bytes = (u64)block_size * iterations;
    u64 mib_per_second = (bytes >> 20) * timer_hz() / elapsed;

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Memory bandwidth\n");
    console_reset_color();
    kprintf("  memset %llu MiB in %llu tick(s) -> %llu MiB/s\n",
            bytes >> 20, elapsed, mib_per_second);
    kfree(buffer);

    volatile u64 accumulator = 0;
    u64 tsc_start = cpu_rdtsc();
    for (u32 i = 0; i < 1000000; i++)
        accumulator += i;
    u64 tsc_end = cpu_rdtsc();
    u64 cycles = tsc_end - tsc_start;

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("TSC benchmark\n");
    console_reset_color();
    kprintf("  1,000,000 ALU iterations in %llu cycles (%llu cycles/iteration)\n",
            cycles, cycles / 1000000);
    kprintf("  accumulator checksum: %llu\n", accumulator);
}

void cmd_sleep(int argc, char **argv)
{
    if (argc < 2) {
        print_usage("sleep <milliseconds>");
        return;
    }

    int requested = atoi(argv[1]);
    if (requested < 1)
        requested = 1;
    if (requested > 60000)
        requested = 60000;

    u64 before = timer_ticks();
    timer_sleep_ms((u32)requested);
    u64 after = timer_ticks();
    kprintf("Slept %d ms (%llu ticks elapsed)\n", requested, after - before);
}

void cmd_desktop(int argc, char **argv)
{
    if (!console_has_framebuffer()) {
        print_error("desktop: no framebuffer (boot with GOP graphics)");
        return;
    }
    desktop_run(argc, argv);
}

void cmd_color(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "reset") == 0) {
        console_reset_color();
        kprintf("Console colours reset.\n");
        return;
    }

    int foreground = atoi(argv[1]);
    int background = argc > 2 ? atoi(argv[2]) : 0;
    if (foreground < 0) foreground = 0;
    if (foreground > 15) foreground = 15;
    if (background < 0) background = 0;
    if (background > 15) background = 15;

    console_set_color((enum console_color)foreground,
                      (enum console_color)background);
    kprintf("Colour sample: fg %d on bg %d\n", foreground, background);
    kprintf("The quick brown fox jumps over the lazy dog.\n");
    console_reset_color();
}

void cmd_neofetch(int, char **)
{
    static const char *const logo[] = {
        "  #######  ##   ##  ",
        "      ###   ## ##   ",
        "     ###     ###    ",
        "    ###       ##    ",
        "  #######     ##    ",
        "                    ",
        "                    ",
        "                    ",
    };

    const struct boot_info *boot = boot_get_info();
    const struct cpu_info *cpu = cpu_get_info();
    struct pmm_stats physical;
    pmm_get_stats(&physical);

    char uptime_text[64];
    format_uptime(uptime_text, sizeof(uptime_text));

    char cpu_brand[56];
    copy_trunc(cpu_brand, sizeof(cpu_brand), cpu->brand);

    char display[64];
    if (boot && boot->framebuffer_base)
        ksnprintf(display, sizeof(display), "%ux%u %s", boot->width,
                  boot->height, pixel_format_name(boot->pixel_format));
    else
        ksnprintf(display, sizeof(display), "serial console");

    char info[8][100];
    ksnprintf(info[0], sizeof(info[0]), "OS: Zhaoxu OS %s (%s)",
              kVersion, kCodename);
    ksnprintf(info[1], sizeof(info[1]),
              "Kernel: Yuanshuo, higher-half x86_64 UEFI");
    ksnprintf(info[2], sizeof(info[2]), "Uptime: %s", uptime_text);
    ksnprintf(info[3], sizeof(info[3]), "CPU: %s", cpu_brand);
    ksnprintf(info[4], sizeof(info[4]),
              "Memory: %llu MiB free / %llu MiB",
              (physical.free_pages * PAGE_SIZE) >> 20,
              (physical.managed_pages * PAGE_SIZE) >> 20);
    ksnprintf(info[5], sizeof(info[5]), "Display: %s", display);
    ksnprintf(info[6], sizeof(info[6]),
              "Drivers: Ada serial, PIT, PS/2, GOP");
    ksnprintf(info[7], sizeof(info[7]),
              "Languages: FASM + C + Ada + C++");

    for (usize i = 0; i < 8; i++) {
        console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
        kprintf("%s", logo[i]);
        console_set_color(i == 0 ? COLOR_WHITE : COLOR_LIGHT_GRAY,
                          COLOR_BLACK);
        kprintf("  %s\n", info[i]);
    }
    console_reset_color();
}

void cmd_reboot(int, char **)
{
    console_set_color(COLOR_YELLOW, COLOR_BLACK);
    kprintf("Rebooting through ACPI...\n");
    console_reset_color();
    acpi_reboot();
}

void cmd_shutdown(int, char **)
{
    console_set_color(COLOR_YELLOW, COLOR_BLACK);
    kprintf("Powering off through ACPI...\n");
    console_reset_color();
    acpi_poweroff();
}

void cmd_panic(int, char **)
{
    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("Triggering a deliberate page fault at 0xffffdead00000000...\n");
    console_reset_color();
    volatile u64 *invalid = (volatile u64 *)0xffffdead00000000ULL;
    *invalid = 0x5A4F5331;
}

int tokenize(char *line, char **argv, int maximum)
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

void execute(char *line)
{
    char *argv[kMaxArgs];
    int argc = tokenize(line, argv, kMaxArgs);

    if (argc == 0)
        return;

    for (usize i = 0; i < kCommandCount; i++) {
        if (strcmp(argv[0], kCommands[i].name) == 0) {
            kCommands[i].handler(argc, argv);
            return;
        }
    }

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("%s: command not found. Try 'help'.\n", argv[0]);
    console_reset_color();
}

void print_banner()
{
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Zhaoxu OS %s \"%s\" - Yuanshuo kernel\n", kVersion, kCodename);
    console_reset_color();
    console_set_color(COLOR_DARK_GRAY, COLOR_BLACK);
    kprintf("FASM boot | C kernel | Ada drivers | C++ shell\n");
    kprintf("Command-Shell 已就绪：输入 help 查看命令，支持中文与 .zxe 程序。\n");
    console_reset_color();
    kprintf("Type 'help' for the command list, 'neofetch' for a summary.\n\n");
}

} // namespace

extern "C" void shell_run(void)
{
    print_banner();

    for (;;) {
        print_prompt();
        g_line_length = 0;
        bool cancelled = false;

        for (;;) {
            int c;
            while ((c = console_read_char()) < 0)
                hlt();

            if (c == '\n' || c == '\r') {
                console_putc('\n');
                break;
            }
            if (c == 3) {                       /* Ctrl+C */
                console_write("^C\n");
                cancelled = true;
                break;
            }
            if (c == 12) {                      /* Ctrl+L */
                console_clear();
                print_prompt();
                g_line_length = 0;
                continue;
            }
            if (c == '\b' || c == 0x7F) {
                if (g_line_length > 0) {
                    g_line_length--;
                    console_write("\b \b");
                }
                continue;
            }
            if (c >= 32 && c < 127 && g_line_length + 1 < kMaxLine) {
                g_line[g_line_length++] = (char)c;
                console_putc((char)c);
            }
        }

        if (cancelled)
            continue;

        g_line[g_line_length] = 0;
        execute(g_line);
    }
}
