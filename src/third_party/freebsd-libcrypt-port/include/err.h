/* BSD err(3) family used by libcrypt on allocation failure. */
#ifndef ZHAOXU_ERR_PORT_H
#define ZHAOXU_ERR_PORT_H

#include <stdarg.h>

void zos_warn(const char *format, ...);
void zos_warnx(const char *format, ...);
void zos_err(int status, const char *format, ...) __attribute__((noreturn));
void zos_errx(int status, const char *format, ...) __attribute__((noreturn));

#define warn zos_warn
#define warnx zos_warnx
#define err zos_err
#define errx zos_errx

#endif
