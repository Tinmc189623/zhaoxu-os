#ifndef ZHAOXU_SYS_STATVFS_H
#define ZHAOXU_SYS_STATVFS_H
#include <sys/types.h>
struct statvfs {
    unsigned long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail;
    unsigned long f_files, f_ffree, f_favail, f_fsid, f_flag, f_namemax;
};
#define ST_RDONLY 0x1

#include <sys/sysinfo.h>
int statvfs(const char *path, struct statvfs *buffer);
int fstatvfs(int fd, struct statvfs *buffer);
#endif
