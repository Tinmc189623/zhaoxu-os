#ifndef ZHAOXU_DIRENT_H
#define ZHAOXU_DIRENT_H

/* newlib ships dirent.h but x86_64-elf has no sys/dirent.h, so its header
 * deliberately fails.  This overlay supplies the POSIX surface nano and
 * ncurses need on top of the kernel's readdir syscall. */
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

typedef struct __zos_dir_stream DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);

#endif
