/*
 * Zhaoxu OS - OS_5P FreeBSD personality: console port.
 *
 * The core owns COM1.  The personality's cn(4) backend forwards kernel printf
 * output to the core console through the service table, so both kernels share
 * one serial line while the core keeps the UART.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/cons.h>
#include <sys/systm.h>

#include "core_abi.h"
#include "vfs.h"

static const struct zx_core_services *zx;

/* The core console always exists, so the backend is always usable. */
static void
zx_cnprobe(struct consdev *cp)
{
	cp->cn_pri = CN_REMOTE;
}

static void
zx_cninit(struct consdev *cp)
{
	(void)cp;
}

static void
zx_cnterm(struct consdev *cp)
{
	(void)cp;
}

static void
zx_cngrab(struct consdev *cp)
{
	(void)cp;
}

static void
zx_cnungrab(struct consdev *cp)
{
	(void)cp;
}

/* Polled input stays unimplemented until traps forward the keys. */
static int
zx_cngetc(struct consdev *cp)
{
	(void)cp;
	return (-1);
}

/* Put one byte on the core console; cnputc expands newlines itself. */
static void
zx_cnputc(struct consdev *cp, int c)
{
	char ch = (char)c;

	(void)cp;
	if (zx != NULL && zx->console_write != NULL)
		zx->console_write(&ch, 1);
}

static const struct consdev_ops zx_cons_ops = {
	.cn_probe = zx_cnprobe,
	.cn_init = zx_cninit,
	.cn_term = zx_cnterm,
	.cn_getc = zx_cngetc,
	.cn_putc = zx_cnputc,
	.cn_grab = zx_cngrab,
	.cn_ungrab = zx_cnungrab,
};

static struct consdev zx_cons = {
	.cn_ops = &zx_cons_ops,
	.cn_pri = CN_REMOTE,
	.cn_name = "zxcore",
};

/* Port entry: the core passes the filled service table. */
int
freebsd_personality_port_start(struct zx_core_services *services)
{
	static const char hello[] = "OS_5P: FreeBSD personality executing\n";

	zx = services;
	if (services == NULL || services->version != ZX_CORE_SERVICES_VERSION)
		return (EINVAL);
	/* The VFS provider registers itself from SI_SUB_MOUNTROOT, after the
	 * vnode layer and root mount exist; it needs this table to reach
	 * vfs_register. */
	freebsd_vfs_set_services(services);
	/* First prove the personality runs: write straight through the table,
	 * before any FreeBSD console state is touched.  cnadd() is enabled once
	 * this boot is known to reach the core's scheduler. */
	if (services->console_write != NULL) {
		services->console_write(hello, (zx_u32)(sizeof(hello) - 1));
		services->console_write("OS_5P: console table bound\n", 27);
	}
	/* Probe the FreeBSD runtime itself: printf lives in the FreeBSD object
	 * set.  If it prints, libkern and the printf path execute before any
	 * sysinit; if it faults, the machine port must initialise the console
	 * and thread0 before mi_startup. */
	printf("OS_5P: FreeBSD libkern printf\n");
	(void)zx_cons;
	return (0);
}
