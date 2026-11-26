/*
 * Zhaoxu OS - system information syscalls (103 uname, 104 access,
 * 105 readlink, 112 last_error).
 *
 * uname carries the real release from version.h.  access answers existence
 * only: every process runs with the system authority, so mode bits add
 * nothing.  readlink has no symbolic links to follow and reports ENOSYS.
 */
#include "process_internal.h"

#include <string.h>
#include <thread.h>
#include <types.h>
#include <version.h>
#include <vfs.h>

#define ENOENT_CODE 2
#define ENOSYS_CODE 38
#define EFAULT_CODE 14

#define UTS_FIELD 65

struct zos_utsname {
    char sysname[UTS_FIELD];
    char nodename[UTS_FIELD];
    char release[UTS_FIELD];
    char version[UTS_FIELD];
    char machine[UTS_FIELD];
    char domainname[UTS_FIELD];
};

/* Copy a literal into a fixed-size utsname field. */
static void uts_set(char *field, const char *value)
{
    usize length = strlen(value);
    if (length >= UTS_FIELD)
        length = UTS_FIELD - 1;
    memcpy(field, value, length);
    field[length] = '\0';
}

long sys_uname(struct process *process, u64 pointer)
{
    if (user_range_ok(process, pointer, sizeof(struct zos_utsname)) != 1)
        return -EFAULT_CODE;
    struct zos_utsname name;
    memset(&name, 0, sizeof(name));
    uts_set(name.sysname, "Zhaoxu");
    uts_set(name.nodename, "zhaoxu");
    uts_set(name.release, ZHAOXU_KERNEL_VERSION);
    uts_set(name.version, "Zhaoxu OS " ZHAOXU_VERSION " (" ZHAOXU_BUILD ")");
    uts_set(name.machine, "x86_64");
    uts_set(name.domainname, "(none)");
    return process_user_write(process, pointer, &name, sizeof(name)) == 0
               ? 0 : -EFAULT_CODE;
}

/* access: existence check through the VFS; mode is accepted and ignored. */
long sys_access(struct process *process, u64 path_pointer, u64 mode)
{
    (void)mode;
    char path[256];
    if (copy_path_from_user(process, path, sizeof(path), path_pointer) != 0)
        return -EFAULT_CODE;
    int is_dir = 0;
    u64 size = 0;
    return vfs_stat(path, &is_dir, &size) == 0 ? 0 : -ENOENT_CODE;
}

/* readlink: the filesystem has no symbolic links yet. */
long sys_readlink(struct process *process, u64 path_pointer, u64 buffer,
                  u64 capacity)
{
    (void)process;
    (void)path_pointer;
    (void)buffer;
    (void)capacity;
    return -ENOSYS_CODE;
}

/* last_error: consume the errno recorded for the previous legacy call. */
long sys_last_error(void)
{
    struct thread *thread = sched_current();
    if (!thread)
        return 0;
    i32 error = thread->last_error;
    thread->last_error = 0;
    return error;
}
