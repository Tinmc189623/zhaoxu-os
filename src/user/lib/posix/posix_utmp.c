/*
 * Zhaoxu OS - utmp access.
 *
 * Login accounting needs /var/run/utmp written by login and read by who.
 * Neither the tty server nor Thresh exists yet, so the enumerator reports
 * an empty database instead of inventing records.
 */
#include <stddef.h>
#include <utmpx.h>

struct utmpx *getutxent(void)
{
    return NULL;
}

void setutxent(void)
{
}

void endutxent(void)
{
}
