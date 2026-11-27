/* Zhaoxu OS - password hashing entry point.
 *
 * tcsh links crypt() for its `lock` builtin.  Zhaoxu has no DES crypt
 * implementation, so the call reports the missing capability instead of
 * returning a hash that would not verify. */
#include "posix_glue.h"

#include <errno.h>
#include <stddef.h>

char *crypt(const char *key, const char *salt)
{
    (void)key;
    (void)salt;
    errno = ENOSYS;
    return NULL;
}
