#ifndef ZHAOXU_FREEBSD_VFS_H
#define ZHAOXU_FREEBSD_VFS_H

#include "core_abi.h"

/* The FreeBSD vnode-backed provider.  The port hands over the core service
 * table; a SYSINIT at mountroot time registers the provider through the
 * table's vfs_register, so the handover happens only once FreeBSD's vnode
 * layer and root mount exist.  Until then the core's transitional rootfs
 * serves path operations. */
const struct zx_vfs_provider *freebsd_vfs_provider(void);
void freebsd_vfs_set_services(struct zx_core_services *services);

#endif
