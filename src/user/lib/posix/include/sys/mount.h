/* mount/statfs surface: the kernel keeps no user-visible mount list and
 * exports no mount syscall, so the calls fail with ENOSYS. */
#ifndef ZHAOXU_SYS_MOUNT_H
#define ZHAOXU_SYS_MOUNT_H

#include <sys/types.h>

struct statfs {
    unsigned long f_type;
    unsigned long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_fsid;
    unsigned long f_namelen;
    unsigned long f_frsize;
    unsigned long f_flags;
    unsigned long f_spare[4];
    char f_fstypename[16];
    char f_mntonname[80];
    char f_mntfromname[80];
};

#define MNT_WAIT 1
#define MNT_NOWAIT 2

#define MS_RDONLY 0x0001
#define MS_NOSUID 0x0002
#define MS_NODEV 0x0004
#define MS_NOEXEC 0x0008
#define MS_REMOUNT 0x0020
#define MS_BIND 0x1000
#define MS_REC 0x4000
#define MS_REC 0x4000
#define MS_NOATIME 0x0400
#define MS_NODIRATIME 0x0800
#define MS_SILENT 0x8000
#define MS_RELATIME 0x200000
#define MS_SYNCHRONOUS 0x0010
#define MS_SHARED 0x100000
#define MS_PRIVATE 0x40000
#define MS_SLAVE 0x80000
#define MS_MOVE 0x2000
#define MS_UNBINDABLE 0x20000

#define MNT_RDONLY 0x00000001
#define MNT_SYNCHRONOUS 0x00000002
#define MNT_NOEXEC 0x00000004
#define MNT_NOSUID 0x00000008

#define MNT_FORCE 0x00000001
#define MNT_DETACH 0x00000002

int statfs(const char *path, struct statfs *buffer);
int fstatfs(int fd, struct statfs *buffer);
int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data);
int umount(const char *target);
int umount2(const char *target, int flags);
int getmntinfo(struct statfs **buffer, int flags);

#endif
