/* stat surface for the Zhaoxu newlib target.
 *
 * newlib declares stat but not lstat; the kernel has no symbolic links, so
 * lstat is implemented as stat (posix_path.c). */
#ifndef ZHAOXU_SYS_STAT_OVERLAY_H
#define ZHAOXU_SYS_STAT_OVERLAY_H

#include_next <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

int lstat(const char *path, struct stat *buffer);

#ifdef __cplusplus
}
#endif

#endif
