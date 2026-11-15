/*
 * Zhaoxu OS - ncurses smoke test.
 *
 * Initialises curses against the Zhaoxu tty, prints one line through the
 * ncurses screen buffer, leaves curses and reports the detected geometry on
 * the plain console.  The terminfo entry is /usr/share/terminfo/z/zhaoxu.
 */
#include <curses.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    setenv("TERM", "zhaoxu", 1);
    setenv("TERMINFO", "/usr/share/terminfo", 1);
    initscr();
    int cols = COLS;
    int rows = LINES;
    printw("ncurses hello %dx%d", cols, rows);
    refresh();
    endwin();
    printf("ncurses hello: %dx%d ok\n", cols, rows);
    return 0;
}
