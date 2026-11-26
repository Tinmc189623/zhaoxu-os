/*
 * Zhaoxu OS - Terminal.
 *
 * An Aqua Desktop Environment client that owns a pseudo-terminal master and
 * runs a shell on the slave side.  Keys go to the master, shell output comes
 * back on the master, and the window shows an 80x25 text grid.  There is no
 * line discipline in the kernel yet, so echo and backspace are handled here.
 */
#include <ade_client.h>
#include "userlib.h"

#define TERM_COLS 80
#define TERM_ROWS 25
#define WINDOW_W 720
#define WINDOW_H 480

static char g_cells[TERM_ROWS][TERM_COLS];
static int g_row;
static int g_col;
static int g_master_read;
static int g_master_write;

/* ---- screen buffer ---------------------------------------------------- */

static void grid_clear(void)
{
    for (int row = 0; row < TERM_ROWS; row++)
        for (int col = 0; col < TERM_COLS; col++)
            g_cells[row][col] = ' ';
    g_row = 0;
    g_col = 0;
}

static void scroll_up(void)
{
    for (int row = 1; row < TERM_ROWS; row++)
        for (int col = 0; col < TERM_COLS; col++)
            g_cells[row - 1][col] = g_cells[row][col];
    for (int col = 0; col < TERM_COLS; col++)
        g_cells[TERM_ROWS - 1][col] = ' ';
}

static void put_char(char character)
{
    if (character == '\n') {
        g_col = 0;
        g_row++;
    } else if (character == '\r') {
        g_col = 0;
    } else if (character == '\b') {
        if (g_col > 0) {
            g_col--;
            g_cells[g_row][g_col] = ' ';
        }
    } else if (character == '\t') {
        int stop = (g_col / 8 + 1) * 8;
        while (g_col < stop && g_col < TERM_COLS)
            g_cells[g_row][g_col++] = ' ';
    } else if (character >= 0x20) {
        g_cells[g_row][g_col++] = character;
        if (g_col >= TERM_COLS) {
            g_col = 0;
            g_row++;
        }
    }
    if (g_row >= TERM_ROWS) {
        scroll_up();
        g_row = TERM_ROWS - 1;
    }
}

/* Strip ANSI escape sequences: the grid has no attributes yet, but keeping
 * the brackets out of the text keeps the window readable. */
static void feed(const char *data, long length)
{
    int skipping = 0;
    for (long i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)data[i];
        if (skipping) {
            if (byte >= 0x40 && byte <= 0x7E)
                skipping = 0;
            continue;
        }
        if (byte == 0x1B) {
            skipping = 1;
            continue;
        }
        put_char((char)byte);
    }
}

static void draw(long channel)
{
    ade_clear(channel, 0x101014);
    char line[TERM_COLS + 1];
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++)
            line[col] = g_cells[row][col];
        line[TERM_COLS] = 0;
        for (int end = TERM_COLS - 1; end >= 0 && line[end] == ' '; end--)
            line[end] = 0;
        ade_text(channel, 12, 8 + row * 18, 0xD8DEE9, line);
    }
    ade_commit(channel);
}

/* ---- shell ------------------------------------------------------------ */

static long start_shell(int slave_read, int slave_write)
{
    long pid = sys_fork();
    if (pid == 0) {
        sys_dup2(slave_read, 0);
        sys_dup2(slave_write, 1);
        sys_dup2(slave_write, 2);
        sys_close(g_master_read);
        sys_close(g_master_write);
        char *argv[3] = { "bash", "-i", 0 };
        sys_execv("/bin/bash", argv);
        sys_exit(127);
    }
    return pid;
}

/* ---- key input -------------------------------------------------------- */

static void send_key(int key)
{
    char byte = (char)key;
    if (key == '\r' || key == '\n') {
        put_char('\n');
        byte = '\n';
    } else if (key == 0x7F || key == '\b') {
        if (g_col > 0) {
            g_col--;
            g_cells[g_row][g_col] = ' ';
        }
        byte = 0x7F;
    } else if (key >= 0x20 && key < 0x7F && g_col < TERM_COLS) {
        g_cells[g_row][g_col++] = (char)key;
    } else if (key < 0x20) {
        /* Ctrl combinations go through untouched. */
    } else {
        return;
    }
    sys_write(g_master_write, &byte, 1);
}

int user_main(long *stack)
{
    (void)stack;

    int fds[4];
    if (sys_pty(fds) != 0)
        return 1;
    g_master_read = fds[0];
    g_master_write = fds[3];

    long channel = ade_connect();
    if (channel <= 0)
        return 1;
    if (ade_create_window(channel, WINDOW_W, WINDOW_H, "Terminal", 'T',
                          0x1F6FEB) != 0)
        return 1;

    long shell = start_shell(fds[2], fds[1]);
    if (shell <= 0)
        return 1;

    grid_clear();
    draw(channel);

    for (;;) {
        struct ade_message message;
        while (ade_poll(channel, &message) == 0) {
            if (message.type == ADE_EV_KEY)
                send_key((int)message.extra);
            if (message.type == ADE_EV_CLOSE)
                return 0;
        }

        struct pollfd wait = { g_master_read, POLLIN, 0 };
        sys_poll(&wait, 1, 20);
        if (wait.revents & POLLIN) {
            char buffer[256];
            long got = sys_read(g_master_read, buffer, sizeof(buffer));
            if (got > 0)
                feed(buffer, got);
        }
        draw(channel);
        sys_sleep(10);
    }
}
