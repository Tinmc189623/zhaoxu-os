/*
 * Zhaoxu OS - syslog client surface.
 *
 * There is no syslogd and no /dev/log yet.  syslog(3) is documented as
 * best-effort: a message that cannot reach a daemon is dropped.  The
 * ident/options/facility state is kept so a future daemon switch can reuse
 * these entry points unchanged.
 */
#include <stdarg.h>
#include <stddef.h>
#include <syslog.h>

static const char *g_ident;
static int g_options;
static int g_facility = LOG_USER;

void openlog(const char *ident, int options, int facility)
{
    g_ident = ident;
    g_options = options;
    g_facility = facility;
    (void)g_options;
    (void)g_facility;
}

void closelog(void)
{
    g_ident = NULL;
    g_options = 0;
}

/* Formatting is not needed while the message has no destination. */
void vsyslog(int priority, const char *format, va_list arguments)
{
    (void)priority;
    (void)format;
    (void)arguments;
}

void syslog(int priority, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsyslog(priority, format, arguments);
    va_end(arguments);
}
