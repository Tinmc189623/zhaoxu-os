/*
 * Zhaoxu OS - FreeBSD vnode-backed VFS provider.
 *
 * Rule 51 and the 2026-11-25 owner directive put the namespace in FreeBSD's
 * vnode layer.  This file implements the core's zx_vfs_provider with the
 * kernel's own syscall helpers (kern_statat/kern_openat/kern_pread/...),
 * so every path operation is executed by FreeBSD, not reimplemented here.
 *
 * It compiles inside the FreeBSD 15.1 tree with _KERNEL defined; the core
 * includes only vfs.h for the provider pointer.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/dirent.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include "vfs.h"

static int zos_vfs_stat(const char *path, int *is_dir, zx_u64 *size)
{
    struct stat status;
    int error;

    if (!path)
        return EINVAL;
    error = kern_statat(curthread, 0, AT_FDCWD, path, UIO_SYSSPACE,
                        &status);
    if (error != 0)
        return error;
    if (is_dir)
        *is_dir = S_ISDIR(status.st_mode) ? 1 : 0;
    if (size)
        *size = (zx_u64)status.st_size;
    return 0;
}

static int zos_vfs_read_at(const char *path, zx_u64 offset, zx_u8 *dst,
                           zx_u64 capacity, zx_u64 *read_out)
{
    int fd, error;
    ssize_t got;

    if (!path || (!dst && capacity))
        return EINVAL;
    error = kern_openat(curthread, AT_FDCWD, path, UIO_SYSSPACE, O_RDONLY,
                        0);
    if (error != 0)
        return error;
    fd = curthread->td_retval[0];
    got = 0;
    if (capacity > 0) {
        error = kern_pread(curthread, fd, dst, (size_t)capacity,
                           (off_t)offset);
        if (error == 0)
            got = curthread->td_retval[0];
    }
    (void)kern_close(curthread, fd);
    if (error != 0)
        return error;
    if (read_out)
        *read_out = (zx_u64)got;
    return 0;
}

static int zos_vfs_write_file(const char *path, const zx_u8 *data,
                              zx_u64 size)
{
    int fd, error;

    if (!path || (size && !data))
        return EINVAL;
    error = kern_openat(curthread, AT_FDCWD, path, UIO_SYSSPACE,
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (error != 0)
        return error;
    fd = curthread->td_retval[0];
    if (size > 0) {
        error = kern_pwrite(curthread, fd, data, (size_t)size, 0);
    }
    (void)kern_close(curthread, fd);
    return error;
}

static int zos_vfs_unlink(const char *path)
{
    if (!path)
        return EINVAL;
    return kern_funlinkat(curthread, AT_FDCWD, path, -1, UIO_SYSSPACE, 0,
                          0);
}

static int zos_vfs_mkdir(const char *path)
{
    if (!path)
        return EINVAL;
    return kern_mkdirat(curthread, AT_FDCWD, path, UIO_SYSSPACE, 0755);
}

static int zos_vfs_rmdir(const char *path)
{
    if (!path)
        return EINVAL;
    return kern_frmdirat(curthread, AT_FDCWD, path, -1, UIO_SYSSPACE, 0);
}

static int zos_vfs_rename(const char *from, const char *to)
{
    if (!from || !to)
        return EINVAL;
    return kern_renameat(curthread, AT_FDCWD, from, AT_FDCWD, to,
                         UIO_SYSSPACE, 0);
}

/* Walk one directory through kern_getdirentries and report each entry. */
static int zos_vfs_list(const char *path, zx_list_fn callback, void *context)
{
    char buffer[4096];
    off_t base = 0;
    int fd, error;

    if (!path)
        return EINVAL;
    error = kern_openat(curthread, AT_FDCWD, path, UIO_SYSSPACE,
                        O_RDONLY | O_DIRECTORY, 0);
    if (error != 0)
        return error;
    fd = curthread->td_retval[0];

    for (;;) {
        ssize_t residual = 0;
        error = kern_getdirentries(curthread, fd, buffer, sizeof(buffer),
                                   &base, &residual, UIO_SYSSPACE);
        if (error != 0)
            break;
        if (curthread->td_retval[0] == 0)
            break;
        for (size_t used = 0; used + sizeof(struct dirent) <=
             sizeof(buffer);) {
            struct dirent *entry = (struct dirent *)(buffer + used);
            if (entry->d_reclen == 0 ||
                used + entry->d_reclen > sizeof(buffer))
                break;
            if (entry->d_namlen > 0 &&
                !(entry->d_name[0] == '.' && entry->d_namlen == 1) &&
                !(entry->d_name[0] == '.' && entry->d_name[1] == '.' &&
                  entry->d_namlen == 2) && callback) {
                callback(entry->d_name, 0, entry->d_type == DT_DIR, context);
            }
            used += entry->d_reclen;
        }
        if (curthread->td_retval[0] <= 0)
            break;
    }
    (void)kern_close(curthread, fd);
    return error;
}

static const struct zx_vfs_provider g_freebsd_vfs = {
    .name = "freebsd",
    .list = zos_vfs_list,
    .stat = zos_vfs_stat,
    .read_at = zos_vfs_read_at,
    .write_file = zos_vfs_write_file,
    .unlink = zos_vfs_unlink,
    .mkdir = zos_vfs_mkdir,
    .rmdir = zos_vfs_rmdir,
    .rename = zos_vfs_rename,
};

const struct zx_vfs_provider *freebsd_vfs_provider(void)
{
    return &g_freebsd_vfs;
}

/* The port stores the validated core service table for the SYSINIT below. */
static struct zx_core_services *g_core_services;

void freebsd_vfs_set_services(struct zx_core_services *services)
{
    g_core_services = services;
}

/* Hand the namespace over after the root filesystem is mounted.  A SYSINIT
 * cannot do this: every sysinit runs before the init kthread calls
 * vfs_mountroot(), so the handover waits for the vfs_mounted event and
 * registers exactly once, for the root mount. */
static int g_vfs_registered;

static void zos_vfs_mounted(void *arg, struct mount *mp, struct vnode *vp,
                            struct thread *td)
{
    (void)arg;
    (void)vp;
    (void)td;
    if (mp == NULL || (mp->mnt_flag & MNT_ROOTFS) == 0)
        return;
    if (g_vfs_registered)
        return;
    g_vfs_registered = 1;
    if (g_core_services != NULL && g_core_services->vfs_register != NULL)
        (void)g_core_services->vfs_register(&g_freebsd_vfs);
}

static void zos_vfs_init(void *arg)
{
    (void)arg;
    EVENTHANDLER_REGISTER(vfs_mounted, zos_vfs_mounted, NULL,
                          EVENTHANDLER_PRI_ANY);
}

SYSINIT(zos_vfs_provider, SI_SUB_VFS, SI_ORDER_ANY, zos_vfs_init, NULL);
