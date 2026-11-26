/*
 * Zhaoxu OS - BSD err(3) family for the FreeBSD libcrypt port.
 *
 * libcrypt aborts through err()/errx() when it cannot allocate; these print
 * the message with strerror(errno) where the BSD call does and then exit.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>

void zos_warnx(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

void zos_warn(const char *format, ...)
{
    int saved = errno;
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fprintf(stderr, ": %s\n", strerror(saved));
}

void zos_errx(int status, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    exit(status);
}

void zos_err(int status, const char *format, ...)
{
    int saved = errno;
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fprintf(stderr, ": %s\n", strerror(saved));
    exit(status);
}
